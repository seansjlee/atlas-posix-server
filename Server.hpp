#ifndef  SERVER_HPP
#define SERVER_HPP

#include <string>
#include <netinet/in.h>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

class Server {
public:
    Server(int port);
    ~Server();
    void start();

private:
    int port;
    int server_fd;
    struct sockaddr_in address;

    std::vector<std::thread> workers;
    std::queue<int> client_queue;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop_pool;

    void handleClient(int client_socket);
    std::string getFileContents(const std::string& filepath);

    void workerThread();
};

#endif