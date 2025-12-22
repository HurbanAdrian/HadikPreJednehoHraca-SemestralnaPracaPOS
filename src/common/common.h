#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define SHM_NAME "/snake_pos_v0"

#define MAP_WIDTH  40
#define MAP_HEIGHT 20
#define MAX_DLZKA_HADA 100

typedef enum {
    SMER_NONE = 0,
    SMER_HORE,
    SMER_DOLE,
    SMER_VLAVO,
    SMER_VPRAVO,
    SMER_KONIEC
} Smer;

typedef struct {
    int x;
    int y;
} Pozicia;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond_tick;     // server → klient
    pthread_cond_t  cond_vstup;    // klient → server

    bool server_bezi;
    unsigned long tick;

    // klient -> server
    Smer vstup;
    bool novy_vstup;

    // server
    Smer aktualny_smer;

    Pozicia had[MAX_DLZKA_HADA]; // had[0] = hlava
    int dlzka_hada;

    Pozicia ovocie;

} HernyStav;

#endif
