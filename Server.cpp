#include "Server.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include <fstream>

Server::Server(int port) : port(port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port 8080\n";
        exit(1);
    }
}

Server::~Server() {
    close(server_fd);
}

void Server::start() {
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Failed to listen on socket\n";
        exit(1);
    }

    std::cout << "Server is listening on port 8080... Waiting for connections.\n";

    while (true) {
        socklen_t addrlen = sizeof(address);
    
        int new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
    
        if (new_socket < 0) {
            std::cerr << "Failed to accept connection\n";
            exit(1);
        }
    
        std::cout << "Connection accepted!\n";
    
        handleClient(new_socket);
    }
}

void Server::handleClient(int client_socket) {
    char buffer[30000] = {0};
    long bytes_read = read(client_socket, buffer, sizeof(buffer));

    std::string method;
    std::string route = "/";
    std::string version;

    if (bytes_read < 0) {
        std::cerr << "Failed to read from socket\n";
    } else {
        std::string request(buffer);
        std::istringstream iss(request);

        iss >> method >> route >> version;

        std::cout << "--- PARSED REQUEST ---\n";
        std::cout << "Method:  " << method << "\n";
        std::cout << "Route:   " << route << "\n";
        std::cout << "Version: " << version << "\n";
        std::cout << "----------------------\n";
    }

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
                            + "\r\n"
                            + body;
    
    write(client_socket, response.c_str(), response.length());
    std::cout << "Response sent to browser.\n";

    close(client_socket);
}

std::string Server::getFileContents(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);

    if (!file.is_open()) {
        return "";
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}