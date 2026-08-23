#ifndef  SERVER_HPP
#define SERVER_HPP

#include <string>
#include <netinet/in.h>

class Server {
public:
    Server(int port);

    ~Server();

    void start();

private:
    int port;
    int server_fd;
    struct sockaddr_in address;

    void handleClient(int client_socket);

    std::string getFileContents(const std::string& filepath);
};

#endif