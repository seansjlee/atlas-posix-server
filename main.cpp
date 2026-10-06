#include "Server.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    int port = 8080;
    int num_workers = 10;
    std::string doc_root = "./public";

    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) num_workers = std::stoi(argv[2]);
    if (argc > 3) doc_root = argv[3];

    std::cout << "Starting Atlas Server...\n";
    std::cout << "  port=" << port
              << " workers=" << num_workers
              << " doc_root=" << doc_root << "\n";

    Server atlas_server(port, num_workers, doc_root);
    atlas_server.start();

    return 0;
}