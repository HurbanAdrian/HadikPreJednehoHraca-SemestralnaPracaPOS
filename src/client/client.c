#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <termios.h>


#include "../common/common.h"

#define FARBA_RESET   "\033[0m"
#define FARBA_CERVENA "\033[31m"
#define FARBA_ZELENA  "\033[32m"
#define FARBA_MODRA   "\033[34m"
#define FARBA_BIELA   "\033[37m"

struct termios globalny_term;

void cleanup_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &globalny_term);
}

void vypni_echo(struct termios* povodny) {
    struct termios t;
    tcgetattr(STDIN_FILENO, povodny);  // uložíme pôvodný stav
    t = *povodny;
    t.c_lflag &= ~(ECHO | ICANON);      // (ECHO | ICANON) = bitová maska ~(...) = „vypni tieto bity“ &= = aplikuj zmenu    Enter nie je potrebný & klávesy sa nevypisujú
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void zapni_echo(const struct termios* povodny) {
    tcsetattr(STDIN_FILENO, TCSANOW, povodny);
}

Smer precitaj_smer() {
    char c = getchar();

    switch (c) {
        case 'w': return SMER_HORE;
        case 's': return SMER_DOLE;
        case 'a': return SMER_VLAVO;
        case 'd': return SMER_VPRAVO;
        case 'q': return SMER_KONIEC;
        case 'p': return SMER_PAUZA;
        case 'r': return SMER_POKRACUJ;
        default:  return SMER_NONE;
    }
}

void* input_thread(void* arg) {
    HernyStav* stav = (HernyStav*)arg;

    //printf("[CLIENT] Ovládanie: w a s d, q = koniec\n");

    while (1) {
        Smer s = precitaj_smer();
        if (s == SMER_NONE) continue;

        pthread_mutex_lock(&stav->mutex);
        stav->vstup = s;
        stav->novy_vstup = true;
        pthread_cond_signal(&stav->cond_vstup);

        if (s == SMER_KONIEC) {
            stav->client_konci = true;
            pthread_cond_broadcast(&stav->cond_tick);
            pthread_mutex_unlock(&stav->mutex);
            break;
        }

        pthread_mutex_unlock(&stav->mutex);
    }

    return NULL;
}

void vykresli_mapu(HernyStav* stav) {
    char mapa[MAP_HEIGHT][MAP_WIDTH];

    // 1. vyplň prázdno
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            mapa[y][x] = ZNAK_PRAZDNO;
        }
    }

    // 1,5. prekážky
    for (int i = 0; i < stav->pocet_prekazok; i++) {
        int x = stav->prekazky[i].x;
        int y = stav->prekazky[i].y;

        // pre istotu
        if (x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT) {
            mapa[y][x] = ZNAK_STENA;
        }
    }


    // 2. ovocie
    mapa[stav->ovocie.y][stav->ovocie.x] = ZNAK_OVOCIE;

    // 3. telo hada
    if (stav->dlzka_hada > 0) {
        for (int i = stav->dlzka_hada - 1; i > 0; i--) {
            mapa[stav->had[i].y][stav->had[i].x] = ZNAK_TELO;
        }

        // 4. hlava
        mapa[stav->had[0].y][stav->had[0].x] = ZNAK_HLAVA;
    }

    // 5. vykresli
    // horná stena
    printf(FARBA_MODRA "#");
    for (int x = 0; x < MAP_WIDTH; x++) printf("#");
    printf("#\n" FARBA_RESET);

    // vnútro + bočné steny
    for (int y = 0; y < MAP_HEIGHT; y++) {
        printf(FARBA_MODRA "#"); // ľavá stena

        for (int x = 0; x < MAP_WIDTH; x++) {
            char c = mapa[y][x];

            switch (c) {
                case ZNAK_OVOCIE:
                    printf(FARBA_CERVENA "%c" FARBA_RESET, c);
                    break;

                case ZNAK_HLAVA:
                case ZNAK_TELO:
                    printf(FARBA_ZELENA "%c" FARBA_RESET, c);
                    break;

                case ZNAK_STENA:
                    printf(FARBA_MODRA "%c" FARBA_RESET, c);
                    break;

                default:
                    printf(FARBA_BIELA "%c" FARBA_RESET, c);
                    break;
            }
        }

        printf(FARBA_MODRA "#\n"); // pravá stena
    }

    // dolná stena
    printf(FARBA_MODRA "#");
    for (int x = 0; x < MAP_WIDTH; x++) printf("#");
    printf("#\n" FARBA_RESET);
}


void* render_thread(void* arg) {
    HernyStav* stav = (HernyStav*)arg;
    unsigned long posledny_tick = (unsigned long)-1;        // aby sa vykreslila mapa pri prvom spusteni

    while (1) {
        pthread_mutex_lock(&stav->mutex);

        while (stav->tick == posledny_tick && stav->server_bezi) {
            pthread_cond_wait(&stav->cond_tick, &stav->mutex);
        }

        if (stav->client_konci) {
            pthread_mutex_unlock(&stav->mutex);
            break;
        }

        if (!stav->server_bezi) {
            printf("\033[H\033[J"); // Vyčistí obrazovku
            printf(FARBA_CERVENA "GAME OVER\n" FARBA_RESET);
            int finalny_cas = (int)difftime(time(NULL), stav->cas_zaciatku_hry);

            // Korekcia pre časový limit (aby neukazovalo napr. 21s pri limite 20s)
            if (stav->rezim_ukoncenia == UKONCENIE_CASOVE && finalny_cas > stav->limit_casu) {
                finalny_cas = stav->limit_casu;
            }

            if (finalny_cas < 0) finalny_cas = 0;

            printf("Finálne skóre: " FARBA_ZELENA "%d" FARBA_RESET "\n", stav->skore);
            printf("Doba prežitia: " FARBA_MODRA  "%d s" FARBA_RESET "\n\n", finalny_cas);
            printf("Stlač Q pre odpojenie (potom spusti ./client znova pre novú hru)\n");
            fflush(stdout);
            pthread_mutex_unlock(&stav->mutex);
            break;
        }

        if (stav->kolo_skoncilo) {
            printf("\033[H\033[J"); // Vyčistí obrazovku
            printf(FARBA_CERVENA "GAME OVER\n" FARBA_RESET);
            int finalny_cas = (int)difftime(stav->cas_posledneho_hraca, stav->cas_zaciatku_hry);

            // Poistka pre istotu (aby neukázalo -1s pri veľmi rýchlej smrti)
            if (finalny_cas < 0) finalny_cas = 0;

            printf("Finálne skóre: " FARBA_ZELENA "%d" FARBA_RESET "\n", stav->skore);
            printf("Doba prežitia: " FARBA_MODRA  "%d s" FARBA_RESET "\n\n", finalny_cas);
            printf("Stlač Q pre odpojenie (potom spusti ./client znova pre novú hru)\n");
            fflush(stdout);

            pthread_mutex_unlock(&stav->mutex);
            usleep(100000);
            continue; // Preskočí vykresľovanie mapy, lebo už sme vypísali Game Over
        }

        posledny_tick = stav->tick;

        //system("clear");
        //printf("\033[H");
        printf("\033[H\033[J");
        // --- REŽIM 1: PAUZA (HLAVNÉ MENU) ---
        if (stav->hra_pozastavena) {
            printf(FARBA_MODRA "=== HLAVNÉ MENU ===\n" FARBA_RESET);
            printf("Hra je pozastavená.\n\n");
            printf("Stlač " FARBA_ZELENA "'r'" FARBA_RESET " pre návrat do hry\n");
            printf("Stlač " FARBA_CERVENA "'q'" FARBA_RESET " pre úplný koniec\n");

            fflush(stdout);
            pthread_mutex_unlock(&stav->mutex);
            continue; // Nevykresľujeme mapu
        }

        time_t teraz = time(NULL);
        int uplynulo = 0;
        if (stav->hra_spustena || stav->cas_obnovenia != 0) {
            // Čas ukazujeme aj počas odpočítavania
            uplynulo = difftime(teraz, stav->cas_zaciatku_hry);
        }

        printf("Skóre: %d | Čas: %d s\n", stav->skore, uplynulo);

        // Ak beží odpočítavanie, vypíšeme ho vedľa času
        if (stav->cas_obnovenia != 0) {
            int do_startu = stav->cas_obnovenia - teraz;
            if (do_startu > 0)
                printf(FARBA_CERVENA " | Štart o: %d..." FARBA_RESET, do_startu);
            else
                printf(FARBA_ZELENA " | ŠTART!" FARBA_RESET);
        }
        printf("\n");

        vykresli_mapu(stav);
        fflush(stdout);

        pthread_mutex_unlock(&stav->mutex);
    }

    return NULL;
}

int main() {
    struct termios povodny_term;   // LOKÁLNA PREMENNÁ

    tcgetattr(STDIN_FILENO, &globalny_term);
    atexit(cleanup_terminal);
    vypni_echo(&povodny_term);

    // 1. otvor existujúcu zdieľanú pamäť
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    // 2. namapuj pamäť
    HernyStav* stav = mmap(NULL, sizeof(HernyStav),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
    if (stav == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    pthread_mutex_lock(&stav->mutex);
    stav->hrac_pripojeny = true;
    stav->client_konci = false;
    pthread_mutex_unlock(&stav->mutex);

    printf("[CLIENT] Pripojený k serveru\n");

    // 3. vytvor vlákna
    pthread_t t_render, t_input;

    pthread_create(&t_render, NULL, render_thread, stav);
    pthread_create(&t_input,  NULL, input_thread,  stav);

    // 4. počkaj na ukončenie
    pthread_join(t_input, NULL);
    pthread_join(t_render, NULL);

    printf("[CLIENT] Končím\n");
    zapni_echo(&povodny_term);
    return 0;

}
