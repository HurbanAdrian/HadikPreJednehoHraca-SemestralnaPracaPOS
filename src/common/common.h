#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define SHM_NAME "/snake_pos_v0"

typedef struct {
    pthread_mutex_t mutex;
    bool server_bezi;
    unsigned long tick;
} HernyStav;

#endif
