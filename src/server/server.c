#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#include "../common/common.h"

static bool je_na_hadovi(HernyStav* stav, int x, int y) {
    for (int i = 0; i < stav->dlzka_hada; i++) {
        if (stav->had[i].x == x && stav->had[i].y == y) {
            return true;
        }
    }
    return false;
}

bool je_prekazka(HernyStav* stav, int x, int y) {
    for (int i = 0; i < stav->pocet_prekazok; i++) {
        if (stav->prekazky[i].x == x && stav->prekazky[i].y == y) {
            return true;
        }
    }
    return false;
}

static void vygeneruj_ovocie(HernyStav* stav) {
    int x, y;
    do {
        x = rand() % MAP_WIDTH;
        y = rand() % MAP_HEIGHT;
    } while (je_na_hadovi(stav, x, y));

    stav->ovocie.x = x;
    stav->ovocie.y = y;

    printf("[SERVER] Ovocie na (%d,%d)\n", x, y);
}

void generuj_prekazky(HernyStav* stav) {
    stav->pocet_prekazok = 0;

    if (stav->rezim != SVET_S_PREKAZKAMI) {
        return; // v režime bez prekážok negenerujeme nič
    }

    int ciel = MAX_PREKAZKY;

    while (stav->pocet_prekazok < ciel) {
        int x = rand() % MAP_WIDTH;
        int y = rand() % MAP_HEIGHT;

        // 1. neokrajové pole (zjednodušenie dosiahnuteľnosti)
        if (x == 0 || x == MAP_WIDTH - 1 ||
            y == 0 || y == MAP_HEIGHT - 1) {
            continue;
            }

        // 2. nesmie byť na hadovi
        if (je_na_hadovi(stav, x, y)) continue;

        // 3. nesmie byť ovocie
        if (stav->ovocie.x == x && stav->ovocie.y == y) continue;

        // 4. nesmie sa opakovať
        if (je_prekazka(stav, x, y)) continue;

        // OK – pridaj prekážku
        stav->prekazky[stav->pocet_prekazok].x = x;
        stav->prekazky[stav->pocet_prekazok].y = y;
        stav->pocet_prekazok++;
    }
}


static bool je_opacny(Smer a, Smer b) {
    return (a == SMER_HORE  && b == SMER_DOLE) ||
           (a == SMER_DOLE  && b == SMER_HORE) ||
           (a == SMER_VLAVO && b == SMER_VPRAVO) ||
           (a == SMER_VPRAVO && b == SMER_VLAVO);
}

static const char* smer_na_text(Smer s) {
    switch (s) {
        case SMER_HORE: return "HORE";
        case SMER_DOLE: return "DOLE";
        case SMER_VLAVO: return "VLAVO";
        case SMER_VPRAVO: return "VPRAVO";
        case SMER_NONE: return "NONE";
        case SMER_KONIEC: return "KONIEC";
        default: return "?";
    }
}

static void wrap_pozicia(Pozicia* p) {
    if (p->x < 0)
        p->x = MAP_WIDTH - 1;
    else if (p->x >= MAP_WIDTH)
        p->x = 0;

    if (p->y < 0)
        p->y = MAP_HEIGHT - 1;
    else if (p->y >= MAP_HEIGHT)
        p->y = 0;
}


static void posun_hlavu(HernyStav* stav) {
    switch (stav->aktualny_smer) {
        case SMER_HORE:   stav->had[0].y--; break;
        case SMER_DOLE:   stav->had[0].y++; break;
        case SMER_VLAVO:  stav->had[0].x--; break;
        case SMER_VPRAVO: stav->had[0].x++; break;
        default: break;
    }

    wrap_pozicia(&stav->had[0]);
}

static void posun_telo(HernyStav* stav) {
    for (int i = stav->dlzka_hada - 1; i > 0; i--) {
        stav->had[i] = stav->had[i - 1];
    }
}

static bool kolizia_so_sebou(HernyStav* stav) {
    // hlava je had[0]
    for (int i = 1; i < stav->dlzka_hada; i++) {
        if (stav->had[0].x == stav->had[i].x && stav->had[0].y == stav->had[i].y) {
            return true;
        }
    }
    return false;
}

int main() {
    // 1. vytvor zdieľanú pamäť
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    // 2. nastav veľkosť zdieľanej pamäte
    if (ftruncate(shm_fd, sizeof(HernyStav)) == -1) {
        perror("ftruncate");
        return 1;
    }

    // 3. namapuj pamäť
    HernyStav* stav = mmap(NULL, sizeof(HernyStav),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED, shm_fd, 0);
    if (stav == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    // 4. inicializuj mutex ako process-shared
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&stav->mutex, &mattr);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

    pthread_cond_init(&stav->cond_tick, &cattr);
    pthread_cond_init(&stav->cond_vstup, &cattr);

    // 5. inicializuj stav hry
    pthread_mutex_lock(&stav->mutex);
    stav->server_bezi = true;
    stav->tick = 0;
    stav->vstup = SMER_NONE;
    stav->novy_vstup = false;
    stav->aktualny_smer = SMER_VPRAVO; // napr. default doprava

    stav->dlzka_hada = 5;

    // inicializacia hada po X osi
    for (int i = 0; i < stav->dlzka_hada; i++) {
        stav->had[i].x = (MAP_WIDTH / 2) - i;
        stav->had[i].y = MAP_HEIGHT / 2;
    }

    srand(time(NULL));
    vygeneruj_ovocie(stav);

    pthread_mutex_unlock(&stav->mutex);

    printf("[SERVER] Bežím...\n");

    // 6. hlavný cyklus servera
    while (stav->server_bezi) {
        usleep(200000); // 200 ms

        pthread_mutex_lock(&stav->mutex);

        // spracuj vstup od klienta
        if (stav->novy_vstup) {
            if (stav->vstup == SMER_KONIEC) {
                printf("[SERVER] Klient chce ukončiť hru\n");
                stav->server_bezi = false;
            } else if (stav->vstup != SMER_NONE) {
                if (!je_opacny(stav->aktualny_smer, stav->vstup)) {
                    stav->aktualny_smer = stav->vstup;
                }
            }
            stav->novy_vstup = false;
        }

        posun_telo(stav);
        posun_hlavu(stav);

        if (kolizia_so_sebou(stav)) {
            printf("[SERVER] KOLÍZIA SO SEBOU! GAME OVER\n");
            stav->server_bezi = false;
            pthread_cond_broadcast(&stav->cond_tick);
            pthread_mutex_unlock(&stav->mutex);
            break;
        }

        // zjedol ovocie?
        if (stav->had[0].x == stav->ovocie.x && stav->had[0].y == stav->ovocie.y) {

            if (stav->dlzka_hada < MAX_DLZKA_HADA) {
                stav->had[stav->dlzka_hada] = stav->had[stav->dlzka_hada - 1]; // skopíruj chvost
                stav->dlzka_hada++;
            }

            printf("[SERVER] ZJEDENÉ OVOCIE! Dlzka = %d\n", stav->dlzka_hada);

            vygeneruj_ovocie(stav);
        }

        stav->tick++;
        printf("[SERVER] Tick %lu | hlava=(%d,%d) | smer=%s\n",
               stav->tick,
               stav->had[0].x,
               stav->had[0].y,
               smer_na_text(stav->aktualny_smer));

        pthread_cond_broadcast(&stav->cond_tick);

        pthread_mutex_unlock(&stav->mutex);
    }

    printf("[SERVER] Končím\n");

}
