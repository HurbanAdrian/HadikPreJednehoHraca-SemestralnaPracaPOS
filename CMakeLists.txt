cmake_minimum_required(VERSION 3.16)
project(Hadik C)

set(CMAKE_C_STANDARD 11)

add_executable(server
        src/server/server.c
)

add_executable(client
        src/client/client.c
)
