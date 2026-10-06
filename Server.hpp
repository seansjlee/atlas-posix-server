#ifndef  SERVER_HPP
#define SERVER_HPP

#include <string>
#include <mutex>
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

    int port;
    std::string doc_root;
    int server_fd;
    struct sockaddr_in address;

    Poller* poller;
    std::unordered_map<int, Connection> conns;
    std::mutex log_mutex;

    void setNonBlocking(int fd);
    void acceptLoop();
    void onAcceptReady();
    void onReadable(Connection& c);
    void onWritable(Connection& c);
    void closeConn(int fd);

    std::string buildResponse(const std::string& raw_request);
    std::string getFileContents(const std::string& filepath);
    std::string getContentType(const std::string& filepath);

    enum class LogLevel { Info, Warn, Error};
    void log(LogLevel level, const std::string& message);
};

#endif
