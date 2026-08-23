#include "Server.hpp"
#include <iostream>

int main() {
    std::cout << "Starting Atlas Server...\n";

    Server atlas_server(8080);

    atlas_server.start();

    return 0;
}