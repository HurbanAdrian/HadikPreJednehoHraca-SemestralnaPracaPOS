#ifndef COMMON_H
#define COMMON_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define SHM_NAME "/snake_pos_v0"

#define MIN_MAP_WIDTH  10
#define MIN_MAP_HEIGHT 5
#define MAX_MAP_WIDTH  100
#define MAX_MAP_HEIGHT 50
#define MAX_DLZKA_HADA 100
#define MAX_PREKAZKY 500

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
    SMER_KONIEC,
    SMER_PAUZA,
    SMER_POKRACUJ
} Smer;

typedef struct {
    int x;
    int y;
} Pozicia;

typedef enum {
    SVET_BEZ_PREKAZOK = 0,
    SVET_S_PREKAZKAMI = 1
} RezimSveta;

typedef enum {
    UKONCENIE_STANDARDNE = 0,
    UKONCENIE_CASOVE = 1
} RezimUkoncenia;


typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond_tick;     // server → klient
    pthread_cond_t  cond_vstup;    // klient → server

    bool server_bezi;
    unsigned long tick;

    RezimSveta rezim;

    RezimUkoncenia rezim_ukoncenia;

    int sirka;
    int vyska;

    // časovanie
    time_t cas_zaciatku_hry;
    time_t cas_posledneho_hraca;

    int limit_casu;   // v sekundách

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
    bool hra_spustena;

    int skore;
    bool hrac_pripojeny;
    bool client_konci;

    // pauza
    bool hra_pozastavena;
    time_t cas_zaciatku_pauzy;
    time_t cas_obnovenia;

    bool kolo_skoncilo;

} HernyStav;

#endif
