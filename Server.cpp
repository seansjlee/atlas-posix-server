#include "Server.hpp"
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
    : port(port) {
    (void)num_workers;
    signal(SIGPIPE, SIG_IGN);

    char resolved_root[PATH_MAX];
    if (realpath(doc_root.c_str(), resolved_root) == nullptr) {
        std::cerr << "Failed to resolve document root: " << doc_root << "\n";
        exit(1);
    }
    this->doc_root = resolved_root;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set socket options\n";
        close(server_fd);
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port 8080\n";
        exit(1);
    }

    poller = Poller::create();
}

Server::~Server() {
    delete poller;
    close(server_fd);
}

void Server::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void Server::start() {
    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on socket\n";
        exit(1);
    }

    setNonBlocking(server_fd);
    poller->add(server_fd, POLL_READ);

    log(LogLevel::Info, "Server is listening on port " + std::to_string(port) + "...");

    acceptLoop();
}

void Server::acceptLoop() {
    std::vector<PollEvent> events;

    while (true) {
        int n = poller->wait(events, -1);
        if (n < 0) {
            log(LogLevel::Error, "poller wait failed");
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].fd;

            if (fd == server_fd) {
                onAcceptReady();
                continue;
            }

            auto it = conns.find(fd);
            if (it == conns.end()) {
                continue;
            }

            if (events[i].flags & POLL_READ) {
                onReadable(it->second);
            } else if (events[i].flags & POLL_WRITE) {
                onWritable(it->second);
            }
        }
    }
}

void Server::onAcceptReady() {
    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
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
        conns[client] = std::move(c);
        poller->add(client, POLL_READ);
    }
}

void Server::onReadable(Connection& c) {
    char buffer[4096];

    while (true) {
        ssize_t bytes = read(c.fd, buffer, sizeof(buffer));

        if (bytes > 0) {
            c.inbuf.append(buffer, bytes);
            if (c.inbuf.size() > 8192) {
                closeConn(c.fd);
                return;
            }
            continue;
        }

        if (bytes == 0) {
            closeConn(c.fd);
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        closeConn(c.fd);
        return;
    }

    if (c.inbuf.find("\r\n\r\n") == std::string::npos) {
        return;
    }

    c.outbuf = buildResponse(c.inbuf);
    c.sent = 0;
    c.state = ConnState::Writing;
    poller->modify(c.fd, POLL_WRITE);
    onWritable(c);
}

void Server::onWritable(Connection& c) {
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
        closeConn(c.fd);
        return;
    }

    closeConn(c.fd);
}

void Server::closeConn(int fd) {
    poller->remove(fd);
    close(fd);
    conns.erase(fd);
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
    static const std::unordered_map<std::string, std::string> mime_types = {
        {"html", "text/html"},
        {"htm",  "text/html"},
        {"css",  "text/css"},
        {"js",   "application/javascript"},
        {"json", "application/json"},
        {"png",  "image/png"},
        {"jpg",  "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif",  "image/gif"},
        {"svg",  "image/svg+xml"},
        {"ico",  "image/x-icon"},
        {"txt",  "text/plain"},
    };

    size_t slash = filepath.find_last_of('/');
    size_t dot = filepath.find_last_of('.');

    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash) ||
        dot + 1 >= filepath.size()) {
        return "application/octet-stream";
    }

    std::string ext = filepath.substr(dot + 1);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    auto it = mime_types.find(ext);
    if (it != mime_types.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

std::string Server::getFileContents(const std::string& filepath) {
    char resolved_path[PATH_MAX];
    if (realpath(filepath.c_str(), resolved_path) == nullptr) {
        return "";
    }

    std::string safe_path(resolved_path);
    std::string safe_root(doc_root);

    if (safe_root.size() > 1 && safe_root.back() == '/') {
        safe_root.pop_back();
    }

    if (safe_path.size() < safe_root.size() ||
        safe_path.compare(0, safe_root.size(), safe_root) != 0 ||
        (safe_path.size() > safe_root.size() && safe_path[safe_root.size()] != '/')) {
        return "";
    }

    std::ifstream file(safe_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

