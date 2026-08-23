#include "Server.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <climits>
#include <cstdlib>

Server::Server(int port) : port(port), stop_pool(false) {
    signal(SIGPIPE, SIG_IGN);

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

    for (int i = 0; i < 10; ++i) {
        workers.emplace_back(&Server::workerThread, this);
    }
}

Server::~Server() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop_pool = true;
    }

    condition.notify_all();

    for (std::thread& worker: workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    close(server_fd);
}

void Server::start() {
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Failed to listen on socket\n";
        exit(1);
    }

    std::cout << "Server is listening on port " << port << "... Waiting for connections.\n";

    while (true) {
        int new_socket = accept(server_fd, nullptr, nullptr);
    
        if (new_socket < 0) {
            if (errno == EINTR || errno == ECONNABORTED || errno == EMFILE) {
                std::cerr << "Transient accept error, retrying...\n";
                continue;
            }
            std::cerr << "Failed to accept connection\n";
            exit(1);
        }
    
        std::cout << "Connection accepted! Pushing to queue...\n";

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            client_queue.push(new_socket);
        }

        condition.notify_one();
    }
}

bool Server::sendAll(int socket, const std::string& data) {
    size_t total_sent = 0;
    size_t length = data.length();

    while (total_sent < length) {
        ssize_t sent = write(socket, data.c_str() + total_sent, length - total_sent);
        
        if (sent <= 0) {
            return false;
        }
        
        total_sent += sent;
    }

    return true;
}

void Server::handleClient(int client_socket) {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::string raw_request;
    char buffer[4096];

    while (true) {
        long bytes_read = read(client_socket, buffer, sizeof(buffer));
        
        if (bytes_read <= 0) {
            close(client_socket);
            return;
        }

        raw_request.append(buffer, bytes_read);

        if (raw_request.length() > 8192) {
            close(client_socket);
            return;
        }

        if (raw_request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    std::istringstream iss(raw_request);
    std::string method;
    std::string route = "/";
    std::string version;

    iss >> method >> route >> version;

    std::cout << "--- PARSED REQUEST ---\n";
    std::cout << "Method:  " << method << "\n";
    std::cout << "Route:   " << route << "\n";
    std::cout << "Version: " << version << "\n";
    std::cout << "----------------------\n";

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

        std::string filepath = "../public" + route;
        
        body = getFileContents(filepath);

        if (body.empty()) {
            status_code = "404 Not Found";
            body = "404 - File Not Found";
        } else {
            status_code = "200 OK";

            if (filepath.find(".html") != std::string::npos) {
                content_type = "text/html";
            } else if (filepath.find(".css") != std::string::npos) {
                content_type = "text/css";
            } else if (filepath.find(".json") != std::string::npos) {
                content_type = "application/json";
            }
        }
    }

    std::string response = "HTTP/1.1 " + status_code + "\r\n"
                            + "Content-Type: " + content_type + "\r\n"
                            + "Content-Length: " + std::to_string(body.length()) + "\r\n"
                            + "Connection: close\r\n"
                            + "\r\n"
                            + body;
    
    sendAll(client_socket, response);
    std::cout << "Response sent to browser.\n";

    close(client_socket);
}

std::string Server::getFileContents(const std::string& filepath) {
    char resolved_path[PATH_MAX];
    if (realpath(filepath.c_str(), resolved_path) == nullptr) {
        return "";
    }

    char public_root[PATH_MAX];
    if (realpath("../public", public_root) == nullptr) {
        return "";
    }

    std::string safe_path(resolved_path);
    std::string safe_root(public_root);

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

void Server::workerThread() {
    while (true) {
        int client_socket;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            condition.wait(lock, [this]() {
                return !client_queue.empty() || stop_pool;
            });

            if (stop_pool && client_queue.empty()) {
                return; 
            }

            client_socket = client_queue.front();
            client_queue.pop();
        }

        handleClient(client_socket);
    }
}