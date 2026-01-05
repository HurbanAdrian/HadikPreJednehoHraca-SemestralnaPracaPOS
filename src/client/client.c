#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "ui.h"

// Definícia farieb pre main menu (UI modul ich má, ale main ich potrebuje na výpisy)
#define FARBA_RESET   "\033[0m"
#define FARBA_CERVENA "\033[31m"

Smer precitaj_klavesu() {
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

    // OPRAVA: Nekontrolujeme server_bezi v hlavičke, ale vnútri
    while (1) {
        Smer s = precitaj_klavesu();
        if (s == SMER_NONE) {
            continue;
        }

        pthread_mutex_lock(&stav->mutex);

        // 1. Najprv skontrolujeme Q (Koniec) - to má prednosť aj keď server stojí
        if (s == SMER_KONIEC) {
            stav->vstup = s;
            stav->novy_vstup = true;
            stav->client_konci = true; // Dôležité: Klient chce skončiť

            pthread_cond_signal(&stav->cond_vstup);
            pthread_cond_broadcast(&stav->cond_tick); // Zobudíme render vlákno

            pthread_mutex_unlock(&stav->mutex);
            break; // Ukončíme input thread
        }

        // 2. Ak server už nebeží (napr. vypršal čas) a nestlačili sme Q,
        // tak ignorujeme ostatné klávesy alebo končíme.
        if (!stav->server_bezi) {
            pthread_mutex_unlock(&stav->mutex);
            // Ak server skončil, ale my sme nestlačili Q, len čakáme na Q.
            // Takže continue, aby sme načítali ďalšiu klávesu.
            continue;
        }

        // 3. Bufferovanie vstupu (W+A fix)
        while (stav->novy_vstup && stav->server_bezi && !stav->client_konci) {
            pthread_cond_wait(&stav->cond_tick, &stav->mutex);
        }

        // Ak medzitým hra skončila (kým sme čakali v buffery)
        if (!stav->server_bezi || stav->client_konci) {
            pthread_mutex_unlock(&stav->mutex);
            // Tu nebreakneme hneď, necháme cyklus bežať, aby sme mohli zachytiť Q v ďalšej iterácii
            // alebo ak chceme skončiť hneď, musíme si byť istí, že render skončí tiež.
            // Najbezpečnejšie je nechať to na Q.
            continue;
        }

        stav->vstup = s;
        stav->novy_vstup = true;
        pthread_cond_signal(&stav->cond_vstup);
        pthread_mutex_unlock(&stav->mutex);
    }
    return NULL;
}

void* render_thread(void* arg) {
    HernyStav* stav = (HernyStav*)arg;
    unsigned long posledny_tick = (unsigned long)-1;

    while (1) {
        pthread_mutex_lock(&stav->mutex);

        // Čakáme na tick, ALEBO kým klient nechce skončiť, ALEBO kým server nespadne
        while (stav->tick == posledny_tick && stav->server_bezi && !stav->client_konci) {
            pthread_cond_wait(&stav->cond_tick, &stav->mutex);
        }
        posledny_tick = stav->tick;

        // --- OPRAVA ZASEKNUTIA ---
        // Ak používateľ stlačil Q, okamžite končíme
        if (stav->client_konci) {
            pthread_mutex_unlock(&stav->mutex);
            break;
        }

        if (stav->hra_pozastavena) {
            vycisti_obrazovku();
            vykresli_menu_pauza();
            fflush(stdout);
            pthread_mutex_unlock(&stav->mutex);
            continue;
        }

        if (!stav->server_bezi || stav->kolo_skoncilo) {
            vykresli_game_over(stav);
            fflush(stdout);

            // Ak len skončilo kolo (náraz), čakáme na Q alebo pád servera
            if (stav->kolo_skoncilo) {
                 pthread_mutex_unlock(&stav->mutex);
                 usleep(100000);
                 continue;
            } else {
                 pthread_mutex_unlock(&stav->mutex);
                 break;
            }
        }


        vycisti_obrazovku();
        time_t teraz = time(NULL);
        int cas = 0;
        if (stav->hra_spustena) {
            // Ak beží odpočet (3..2..1), čas má "stáť" na hodnote pred pauzou.
            time_t efektivny_teraz = (stav->cas_obnovenia != 0) ? (stav->cas_obnovenia - 3) : teraz;

            cas = difftime(efektivny_teraz, stav->cas_zaciatku_hry);
        }
        printf("Skóre: %d | Čas: %d s", stav->skore, cas);

        if (stav->cas_obnovenia != 0) {
            int odpocet = stav->cas_obnovenia - teraz;
            if (odpocet > 0) {
                printf(FARBA_CERVENA " | Štart o: %d..." FARBA_RESET, odpocet);
            }
        }
        printf("\n");

        vykresli_mapu(stav);
        fflush(stdout);
        pthread_mutex_unlock(&stav->mutex);
    }
    return NULL;
}

int spustit_hru() {
    init_terminal();
    vypni_echo();

    int fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (fd == -1) {
        cleanup_terminal();
        perror("shm_open");
        return 1;
    }
    HernyStav* stav = mmap(NULL, sizeof(HernyStav), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (stav == MAP_FAILED) {
        cleanup_terminal();
        perror("mmap");
        return 1;
    }

    pthread_mutex_lock(&stav->mutex);
    stav->hrac_pripojeny = true;
    stav->client_konci = false;
    pthread_mutex_unlock(&stav->mutex);

    pthread_t tr, ti;
    pthread_create(&tr, NULL, render_thread, stav);
    pthread_create(&ti, NULL, input_thread, stav);

    pthread_join(tr, NULL); // Input thread skončí hneď po stlačení Q
    pthread_join(ti, NULL);

    munmap(stav, sizeof(HernyStav));
    cleanup_terminal();
    return 0;
}

int main() {
    int volba = 0;
    while(1) {
        system("clear");
        printf(FARBA_MODRA "==================================\n");
        printf("          HADÍK - CLIENT          \n");
        printf("==================================\n" FARBA_RESET);
        printf("1. Nová hra (Spustiť server + pripojiť)\n");
        printf("2. Pripojiť sa k existujúcej hre\n");
        printf("3. Koniec\n");
        printf("----------------------------------\n");
        printf("Tvoja voľba: ");

        if (scanf("%d", &volba) != 1) {
            while(getchar()!='\n') {}
            continue;
        }

        if (volba == 3) {
            break;
        }
        if (volba == 2) {
            spustit_hru();
        }
        if (volba == 1) {
            int old_fd = shm_open(SHM_NAME, O_RDWR, 0666);
            if (old_fd != -1) {
                HernyStav* os = mmap(NULL, sizeof(HernyStav), PROT_READ|PROT_WRITE, MAP_SHARED, old_fd, 0);
                if (os != MAP_FAILED) {
                    os->server_bezi = false;
                    pthread_cond_broadcast(&os->cond_tick);
                    munmap(os, sizeof(HernyStav));
                }
                close(old_fd);
                shm_unlink(SHM_NAME);
                usleep(100000);
            }

            int svet=0, rezim=0, limit=0, w=40, h=20;
            printf("Svet (0=Volny, 1=Prekazky): ");
            scanf("%d", &svet);
            printf("Rezim (0=Standard, 1=Cas): ");
            scanf("%d", &rezim);
            if (rezim == 1) {
                printf("Limit (s): ");
                scanf("%d", &limit);
            }

            // --- KONTROLA LIMITOV MAPY ---
            do {
                printf("Zadaj sirku (%d - %d): ", MIN_MAP_WIDTH, MAX_MAP_WIDTH);
                scanf("%d", &w);
                if (w < MIN_MAP_WIDTH || w > MAX_MAP_WIDTH) {
                    printf("Chyba! Sirka musi byt medzi %d a %d.\n", MIN_MAP_WIDTH, MAX_MAP_WIDTH);
                }
            } while (w < MIN_MAP_WIDTH || w > MAX_MAP_WIDTH);

            do {
                printf("Zadaj vysku (%d - %d): ", MIN_MAP_HEIGHT, MAX_MAP_HEIGHT);
                scanf("%d", &h);
                if (h < MIN_MAP_HEIGHT || h > MAX_MAP_HEIGHT) {
                    printf("Chyba! Vyska musi byt medzi %d a %d.\n", MIN_MAP_HEIGHT, MAX_MAP_HEIGHT);
                }
            } while (h < MIN_MAP_HEIGHT || h > MAX_MAP_HEIGHT);
            // -------------------------------------

            pid_t pid = fork();
            if (pid == 0) {
                char as[10], ar[10], al[10], aw[10], ah[10];
                sprintf(as,"%d", svet); sprintf(ar,"%d", rezim);
                sprintf(al,"%d", limit); sprintf(aw,"%d", w); sprintf(ah,"%d", h);
                execlp("./server", "server", as, ar, al, aw, ah, NULL);
                exit(1);
            }
            sleep(1);
            spustit_hru();
        }
    }
    return 0;
}