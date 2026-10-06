#include "Poller.hpp"

#include <unistd.h>
#include <cerrno>

#if defined(__linux__)

#include <sys/epoll.h>

class EpollPoller : public Poller {
public:
    EpollPoller() { epfd = epoll_create1(0); }
    ~EpollPoller() override { if (epfd >= 0) close(epfd); }

    bool add(int fd, uint32_t flags) override    { return ctl(EPOLL_CTL_ADD, fd, flags); }
    bool modify(int fd, uint32_t flags) override  { return ctl(EPOLL_CTL_MOD, fd, flags); }

    bool remove(int fd) override {
        return epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) == 0;
    }

    int wait(std::vector<PollEvent>& out, int timeout_ms) override {
        struct epoll_event evs[1024];
        int n = epoll_wait(epfd, evs, 1024, timeout_ms);
        if (n < 0) return errno == EINTR ? 0 : -1;

        out.clear();
        for (int i = 0; i < n; ++i) {
            uint32_t f = 0;
            if (evs[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) f |= POLL_READ;
            if (evs[i].events & EPOLLOUT) f |= POLL_WRITE;
            out.push_back({evs[i].data.fd, f});
        }
        return n;
    }

private:
    int epfd = -1;

    bool ctl(int op, int fd, uint32_t flags) {
        struct epoll_event ev{};
        ev.data.fd = fd;
        if (flags & POLL_READ)  ev.events |= EPOLLIN;
        if (flags & POLL_WRITE) ev.events |= EPOLLOUT;
        return epoll_ctl(epfd, op, fd, &ev) == 0;
    }
};

Poller* Poller::create() { return new EpollPoller(); }

#elif defined(__APPLE__)

#include <sys/event.h>

class KqueuePoller : public Poller {
public:
    KqueuePoller() { kq = kqueue(); }
    ~KqueuePoller() override { if (kq >= 0) close(kq); }

    bool add(int fd, uint32_t flags) override    { return apply(fd, flags); }
    bool modify(int fd, uint32_t flags) override  { return apply(fd, flags); }

    bool remove(int fd) override {
        struct kevent ev[2];
        EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(kq, ev, 2, nullptr, 0, nullptr);
        return true;
    }

    int wait(std::vector<PollEvent>& out, int timeout_ms) override {
        struct kevent evs[1024];
        struct timespec ts{timeout_ms / 1000, (timeout_ms % 1000) * 1000000};
        int n = kevent(kq, nullptr, 0, evs, 1024, timeout_ms < 0 ? nullptr : &ts);
        if (n < 0) return errno == EINTR ? 0 : -1;

        out.clear();
        for (int i = 0; i < n; ++i) {
            uint32_t f = 0;
            if (evs[i].filter == EVFILT_READ) f |= POLL_READ;
            if (evs[i].filter == EVFILT_WRITE) f |= POLL_WRITE;
            out.push_back({static_cast<int>(evs[i].ident), f});
        }
        return n;
    }

private:
    int kq = -1;

    // kqueue tracks read and write as separate filters, so we enable
    // or delete each one to match the requested flags
    bool apply(int fd, uint32_t flags) {
        struct kevent ev[2];
        EV_SET(&ev[0], fd, EVFILT_READ,
               (flags & POLL_READ) ? (EV_ADD | EV_ENABLE) : EV_DELETE, 0, 0, nullptr);
        EV_SET(&ev[1], fd, EVFILT_WRITE,
               (flags & POLL_WRITE) ? (EV_ADD | EV_ENABLE) : EV_DELETE, 0, 0, nullptr);
        kevent(kq, ev, 2, nullptr, 0, nullptr);
        return true;
    }
};

Poller* Poller::create() { return new KqueuePoller(); }

#else
#error "no supported poller backend for this platform"
#endif
