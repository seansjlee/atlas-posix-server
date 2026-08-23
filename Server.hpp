#ifndef  SERVER_HPP
#define SERVER_HPP

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
};

#endif