#ifndef POLLER_HPP
#define POLLER_HPP

#include <cstdint>
#include <vector>

enum PollFlags : uint32_t {
    POLL_READ  = 1u << 0,
    POLL_WRITE = 1u << 1,
};

struct PollEvent {
    int fd;
    uint32_t flags;
};

class Poller {
public:
    virtual ~Poller() = default;

    virtual bool add(int fd, uint32_t flags) = 0;
    virtual bool modify(int fd, uint32_t flags) = 0;
    virtual bool remove(int fd) = 0;

    // returns ready count, or -1 on error
    virtual int wait(std::vector<PollEvent>& out, int timeout_ms) = 0;

    static Poller* create();
};

#endif
