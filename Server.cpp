#include "Server.hpp"
#include "HttpUtil.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cctype>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <climits>
#include <cstdlib>

void Server::log(LogLevel level, const std::string&message) {
    const char* prefix = "[INFO] ";
    std::ostream* out = &std::cout;

    switch (level) {
        case LogLevel::Warn:
            prefix = "[WARN] ";
            out = &std::cerr;
            break;
        case LogLevel::Error:
            prefix = "[ERROR] ";
            out = &std::cerr;
            break;
        case LogLevel::Info:
            break;
    }

    std::lock_guard<std::mutex> lock(log_mutex);
    *out << prefix << message << "\n";
}

Server::Server(int port, int num_workers, const std::string& doc_root)
    : port(port), num_workers(num_workers) {
    signal(SIGPIPE, SIG_IGN);

    char resolved_root[PATH_MAX];
    if (realpath(doc_root.c_str(), resolved_root) == nullptr) {
        std::cerr << "Failed to resolve document root: " << doc_root << "\n";
        exit(1);
    }
    this->doc_root = resolved_root;
}

Server::~Server() {
    for (std::thread& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void Server::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int Server::openListener() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "Failed to create socket\n";
        exit(1);
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port << "\n";
        exit(1);
    }

    if (listen(fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on socket\n";
        exit(1);
    }

    setNonBlocking(fd);
    return fd;
}

void Server::start() {
    int n = num_workers > 0 ? num_workers : 1;
    log(LogLevel::Info, "Server listening on port " + std::to_string(port)
                        + " with " + std::to_string(n) + " workers...");

    std::vector<Worker> workers(n);
    for (int i = 0; i < n; ++i) {
        workers[i].listen_fd = openListener();
        workers[i].poller = Poller::create();
        workers[i].poller->add(workers[i].listen_fd, POLL_READ);
    }

    for (int i = 1; i < n; ++i) {
        threads.emplace_back(&Server::runWorker, this, std::ref(workers[i]));
    }
    runWorker(workers[0]);
}

void Server::runWorker(Worker& w) {
    std::vector<PollEvent> events;

    while (true) {
        int n = w.poller->wait(events, -1);
        if (n < 0) {
            log(LogLevel::Error, "poller wait failed");
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].fd;

            if (fd == w.listen_fd) {
                onAcceptReady(w);
                continue;
            }

            auto it = w.conns.find(fd);
            if (it == w.conns.end()) {
                continue;
            }

            if (events[i].flags & POLL_READ) {
                onReadable(w, it->second);
            } else if (events[i].flags & POLL_WRITE) {
                onWritable(w, it->second);
            }
        }
    }
}

void Server::onAcceptReady(Worker& w) {
    while (true) {
        int client = accept(w.listen_fd, nullptr, nullptr);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR || errno == ECONNABORTED) {
                continue;
            }
            log(LogLevel::Warn, "accept error");
            break;
        }

        setNonBlocking(client);
        Connection c;
        c.fd = client;
        w.conns[client] = std::move(c);
        w.poller->add(client, POLL_READ);
    }
}

void Server::onReadable(Worker& w, Connection& c) {
    char buffer[4096];

    while (true) {
        ssize_t bytes = read(c.fd, buffer, sizeof(buffer));

        if (bytes > 0) {
            c.inbuf.append(buffer, bytes);
            if (c.inbuf.size() > 8192) {
                closeConn(w, c.fd);
                return;
            }
            continue;
        }

        if (bytes == 0) {
            closeConn(w, c.fd);
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        closeConn(w, c.fd);
        return;
    }

    if (c.inbuf.find("\r\n\r\n") == std::string::npos) {
        return;
    }

    c.outbuf = buildResponse(c.inbuf);
    c.sent = 0;
    c.state = ConnState::Writing;
    w.poller->modify(c.fd, POLL_WRITE);
    onWritable(w, c);
}

void Server::onWritable(Worker& w, Connection& c) {
    while (c.sent < c.outbuf.size()) {
        ssize_t n = write(c.fd, c.outbuf.data() + c.sent, c.outbuf.size() - c.sent);

        if (n > 0) {
            c.sent += n;
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        closeConn(w, c.fd);
        return;
    }

    closeConn(w, c.fd);
}

void Server::closeConn(Worker& w, int fd) {
    w.poller->remove(fd);
    close(fd);
    w.conns.erase(fd);
}

std::string Server::buildResponse(const std::string& raw_request) {
    std::istringstream iss(raw_request);
    std::string method;
    std::string route = "/";
    std::string version;

    iss >> method >> route >> version;

    std::string status_code;
    std::string content_type = "text/plain";
    std::string body;

    if (route.find("..") != std::string::npos) {
        status_code = "403 Forbidden";
        body = "403 - Forbidden: Invalid Path";
    } else if (route == "/api") {
        status_code = "200 OK";
        content_type = "application/json";
        body = "{\"name\": \"Atlas\", \"version\": \"1.0\", \"status\": \"running\"}";
    } else {
        if (route == "/") {
            route = "/index.html";
        }

        std::string filepath = doc_root + route;
        body = getFileContents(filepath);

        if (body.empty()) {
            status_code = "404 Not Found";
            body = "404 - File Not Found";
        } else {
            status_code = "200 OK";
            content_type = getContentType(filepath);
        }
    }

    return "HTTP/1.1 " + status_code + "\r\n"
         + "Content-Type: " + content_type + "\r\n"
         + "Content-Length: " + std::to_string(body.length()) + "\r\n"
         + "Connection: close\r\n"
         + "\r\n"
         + body;
}

std::string Server::getContentType(const std::string& filepath) {
    return http::contentTypeFor(filepath);
}

std::string Server::getFileContents(const std::string& filepath) {
    char resolved_path[PATH_MAX];
    if (realpath(filepath.c_str(), resolved_path) == nullptr) {
        return "";
    }

    if (!http::pathContained(doc_root, resolved_path)) {
        return "";
    }

    std::ifstream file(resolved_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

