#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <termios.h>


#include "../common/common.h"

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
        pthread_mutex_unlock(&stav->mutex);

        if (s == SMER_KONIEC) {
            break;
        }
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

    // 2. ovocie
    mapa[stav->ovocie.y][stav->ovocie.x] = ZNAK_OVOCIE;

    // 3. telo hada
    for (int i = stav->dlzka_hada - 1; i > 0; i--) {
        mapa[stav->had[i].y][stav->had[i].x] = ZNAK_TELO;
    }

    // 4. hlava
    mapa[stav->had[0].y][stav->had[0].x] = ZNAK_HLAVA;

    // 5. vykresli
    // horná stena
    putchar('#');
    for (int x = 0; x < MAP_WIDTH; x++) putchar('#');
    putchar('#');
    putchar('\n');

    // vnútro + bočné steny
    for (int y = 0; y < MAP_HEIGHT; y++) {
        putchar('#'); // ľavá stena
        for (int x = 0; x < MAP_WIDTH; x++) {
            putchar(mapa[y][x]);
        }
        putchar('#'); // pravá stena
        putchar('\n');
    }

    // dolná stena
    putchar('#');
    for (int x = 0; x < MAP_WIDTH; x++) putchar('#');
    putchar('#');
    putchar('\n');
}


void* render_thread(void* arg) {
    HernyStav* stav = (HernyStav*)arg;
    unsigned long posledny_tick = 0;

    while (1) {
        pthread_mutex_lock(&stav->mutex);

        while (stav->tick == posledny_tick && stav->server_bezi) {
            pthread_cond_wait(&stav->cond_tick, &stav->mutex);
        }

        if (!stav->server_bezi) {
            printf("[CLIENT] GAME OVER\n");
            pthread_mutex_unlock(&stav->mutex);
            break;
        }

        posledny_tick = stav->tick;

        //system("clear");
        //printf("\033[H");
        printf("\033[H\033[J");
        printf("[CLIENT] Tick %lu | had %d  \n", stav->tick, stav->dlzka_hada);
        /*for (int i = 0; i < stav->dlzka_hada; i++) {
            printf("(%d,%d) ", stav->had[i].x, stav->had[i].y);
        }
        printf("| ovocie=(%d,%d)\n", stav->ovocie.x, stav->ovocie.y);*/

        vykresli_mapu(stav);
        fflush(stdout);

        pthread_mutex_unlock(&stav->mutex);
    }

    return NULL;
}

int main() {
    struct termios povodny_term;   // LOKÁLNA PREMENNÁ

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
