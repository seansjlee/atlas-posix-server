#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    struct sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Failed to bind to port 8080\n";
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Failed to listen on socket\n";
        return 1;
    }

    std::cout << "Server is listening on port 8080... Waiting for connections.\n";

    socklen_t addrlen = sizeof(address);

    int new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);

    if (new_socket < 0) {
        std::cerr << "Failed to accept connection\n";
        return 1;
    }

    std::cout << "Connection accepted!\n";

    char buffer[30000] = {0};
    long bytes_read = read(new_socket, buffer, sizeof(buffer));

    if (bytes_read < 0) {
        std::cerr << "Failed to read from socket\n";
    } else {
        std::cout << "--- RAW HTTP REQUEST ---\n\n";
        std::cout << buffer << "\n";
        std::cout << "------------------------\n";
    }

    std::string response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: 12\r\n"
                           "\r\n"
                           "Hello World!";
    
    write(new_socket, response.c_str(), response.length());
    std::cout << "Response sent to browser.\n";

    close(new_socket);
    close(server_fd);

    return 0;
}