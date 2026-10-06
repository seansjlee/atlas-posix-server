#ifndef  SERVER_HPP
#define SERVER_HPP

#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <unordered_map>
#include <netinet/in.h>
#include "Poller.hpp"

class Server {
public:
    Server(int port, int num_workers, const std::string& doc_root);
    ~Server();
    void start();

private:
    enum class ConnState { Reading, Writing };

    struct Connection {
        int fd;
        ConnState state = ConnState::Reading;
        std::string inbuf;
        std::string outbuf;
        size_t sent = 0;
    };

    struct Worker {
        int listen_fd = -1;
        Poller* poller = nullptr;
        std::unordered_map<int, Connection> conns;
    };

    int port;
    int num_workers;
    std::string doc_root;

    std::vector<std::thread> threads;
    std::mutex log_mutex;

    void setNonBlocking(int fd);
    int openListener();
    void runWorker(Worker& w);
    void onAcceptReady(Worker& w);
    void onReadable(Worker& w, Connection& c);
    void onWritable(Worker& w, Connection& c);
    void closeConn(Worker& w, int fd);

    std::string buildResponse(const std::string& raw_request);
    std::string getFileContents(const std::string& filepath);
    std::string getContentType(const std::string& filepath);

    enum class LogLevel { Info, Warn, Error};
    void log(LogLevel level, const std::string& message);
};

#endif
