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
    std::mutex log_mutex;
    bool stop_pool;

    void handleClient(int client_socket);
    std::string getFileContents(const std::string& filepath);
    std::string getContentType(const std::string& filepath);

    void workerThread();
    bool sendAll(int socket, const std::string& data);

    enum class LogLevel { Info, Warn, Error};
    void log(LogLevel level, const std::string& message);
};

#endif