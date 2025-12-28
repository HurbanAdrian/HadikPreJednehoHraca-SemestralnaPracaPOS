#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>

#define SHM_NAME "/snake_pos_v0"

#define MAP_WIDTH  40
#define MAP_HEIGHT 20
#define MAX_DLZKA_HADA 100
#define MAX_PREKAZKY 30

#define ZNAK_PRAZDNO ' '
#define ZNAK_STENA   '#'
#define ZNAK_HLAVA   '0'
#define ZNAK_TELO    'o'
#define ZNAK_OVOCIE  '*'


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

typedef enum {
    SVET_BEZ_PREKAZOK = 0,
    SVET_S_PREKAZKAMI = 1
} RezimSveta;


typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond_tick;     // server → klient
    pthread_cond_t  cond_vstup;    // klient → server

    bool server_bezi;
    unsigned long tick;

    RezimSveta rezim;

    // klient -> server
    Smer vstup;
    bool novy_vstup;

    // server
    Smer aktualny_smer;

    Pozicia had[MAX_DLZKA_HADA]; // had[0] = hlava
    int dlzka_hada;

    Pozicia ovocie;

    Pozicia prekazky[MAX_PREKAZKY];
    int pocet_prekazok;

} HernyStav;

#endif
