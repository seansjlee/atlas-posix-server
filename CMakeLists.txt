cmake_minimum_required(VERSION 3.10)
project(Atlas VERSION 1.0)

# Enforce Modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Threads REQUIRED)

# Create the executable from our main file
add_executable(Atlas main.cpp Server.cpp Poller.cpp)
target_compile_options(Atlas PRIVATE -Wall -Wextra)
target_link_libraries(Atlas PRIVATE Threads::Threads)