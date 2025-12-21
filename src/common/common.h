#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define SHM_NAME "/snake_pos_v0"

typedef enum {
    SMER_NONE = 0,
    SMER_HORE,
    SMER_DOLE,
    SMER_VLAVO,
    SMER_VPRAVO,
    SMER_KONIEC
} Smer;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond_tick;     // server → klient
    pthread_cond_t  cond_vstup;    // klient → server

    bool server_bezi;
    unsigned long tick;

    Smer vstup;
    bool novy_vstup;
} HernyStav;

#endif
