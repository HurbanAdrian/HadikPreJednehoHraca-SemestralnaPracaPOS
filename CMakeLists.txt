cmake_minimum_required(VERSION 3.16)
project(Hadik C)

set(CMAKE_C_STANDARD 11)
# Pridáme flagy pre bezpečnosť a vlákna
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pthread -D_DEFAULT_SOURCE")

# Povieme, kde sú hlavičkové súbory (.h), aby sme nemuseli písať dlhé cesty
include_directories(
        src/common
        src/server
        src/client
)

# --- CIEĽ: SERVER ---
# Skladá sa z main servera a hernej logiky
add_executable(server
        src/server/server.c
        src/server/game_logic.c
        src/server/game_logic.h
        src/common/common.h
)
target_link_libraries(server pthread rt)

# --- CIEĽ: CLIENT ---
# Skladá sa z main klienta a UI logiky
add_executable(client
        src/client/client.c
        src/client/ui.c
        src/client/ui.h
        src/common/common.h
)
target_link_libraries(client pthread rt)