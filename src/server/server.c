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
    } while (je_na_hadovi(stav, x, y) || je_prekazka(stav, x, y));

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

int main(int argc, char** argv) {
    RezimSveta svet = SVET_BEZ_PREKAZOK;
    RezimUkoncenia ukoncenie = UKONCENIE_STANDARDNE;
    int limit = 0;

    if (argc >= 2) svet = atoi(argv[1]) ? SVET_S_PREKAZKAMI : SVET_BEZ_PREKAZOK;
    if (argc >= 3) ukoncenie = atoi(argv[2]) ? UKONCENIE_CASOVE : UKONCENIE_STANDARDNE;
    if (ukoncenie == UKONCENIE_CASOVE && argc >= 4) limit = atoi(argv[3]);


    printf("[SERVER] Režim sveta: %s\n", (svet == SVET_S_PREKAZKAMI) ? "S PREKÁŽKAMI" : "BEZ PREKÁŽOK");

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
    stav->rezim = svet;
    stav->rezim_ukoncenia = ukoncenie;
    stav->limit_casu = limit;
    stav->pocet_prekazok = 0;
    stav->hra_spustena = false;
    stav->cas_zaciatku_hry = 0;
    stav->cas_posledneho_hraca = time(NULL);

    stav->hrac_pripojeny = false;

    stav->skore = 0;
    stav->dlzka_hada = 0;

    srand(time(NULL));
    vygeneruj_ovocie(stav);
    generuj_prekazky(stav);

    pthread_mutex_unlock(&stav->mutex);

    printf("[SERVER] Bežím...\n");

    // 6. hlavný cyklus servera
    while (stav->server_bezi) {
        usleep(200000); // 200 ms

        pthread_mutex_lock(&stav->mutex);

        time_t teraz = time(NULL);

        if (stav->hrac_pripojeny && stav->dlzka_hada == 0) {
            // nový had
            stav->dlzka_hada = 5;
            stav->aktualny_smer = SMER_VPRAVO;
            stav->hra_spustena = false;   // čakáme na prvý vstup
            stav->kolo_skoncilo = false;

            stav->hra_pozastavena = false; // Zrušíme pauzu z minulej hry
            stav->cas_obnovenia = 0;       // Zrušíme odpočítavanie
            stav->skore = 0;               // Reset skóre

            for (int i = 0; i < stav->dlzka_hada; i++) {
                stav->had[i].x = (MAP_WIDTH / 2) - i;
                stav->had[i].y = MAP_HEIGHT / 2;
            }

            vygeneruj_ovocie(stav);

            printf("[SERVER] Nový had vytvorený po znovupripojení\n");
        }


        // štandardný režim – 10 sekúnd bez aktivity
        if (stav->rezim_ukoncenia == UKONCENIE_STANDARDNE) {
            if (!stav->hrac_pripojeny && difftime(teraz, stav->cas_posledneho_hraca) >= 10) {

                printf("[SERVER] 10 sekúnd bez hráča – končím\n");
                stav->server_bezi = false;
                pthread_cond_broadcast(&stav->cond_tick);
                pthread_mutex_unlock(&stav->mutex);
                break;
            }
        }

        if (stav->rezim_ukoncenia == UKONCENIE_CASOVE) {
            if (stav->hra_spustena && difftime(teraz, stav->cas_zaciatku_hry) >= stav->limit_casu) {

                printf("[SERVER] Vypršal čas hry\n");
                stav->kolo_skoncilo = true;
                stav->cas_posledneho_hraca = time(NULL);
                stav->server_bezi = false;
                pthread_cond_broadcast(&stav->cond_tick);
                pthread_mutex_unlock(&stav->mutex);
                break;
            }
        }


        // čakanie na prvý vstup od klienta
        if (!stav->hra_spustena && !stav->kolo_skoncilo && !stav->hra_pozastavena && stav->cas_obnovenia == 0) {
            if (stav->novy_vstup && stav->vstup != SMER_NONE) {
                stav->hra_spustena = true;
                stav->cas_zaciatku_hry = time(NULL);
                stav->cas_posledneho_hraca = stav->cas_zaciatku_hry;
                printf("[SERVER] Hra spustená klientom\n");
            } else {
                pthread_mutex_unlock(&stav->mutex);
                usleep(100000);
                continue;
            }
        }

        // spracuj vstup od klienta
        if (stav->novy_vstup) {
            // --- PAUZA ---
            if (stav->vstup == SMER_PAUZA && stav->hra_spustena && !stav->hra_pozastavena) {
                stav->hra_pozastavena = true;
                stav->hra_spustena = false; // Aby sa nehýbal
                stav->cas_zaciatku_pauzy = time(NULL); // Uložíme, kedy začala pauza
                printf("[SERVER] Hra pozastavená (Menu)\n");
            }

            // --- POKRAČOVANIE (Návrat z menu) ---
            else if (stav->vstup == SMER_POKRACUJ && stav->hra_pozastavena) {
                stav->hra_pozastavena = false;

                // 1. Zistíme, ako dlho sme stáli
                time_t teraz = time(NULL);
                time_t trvanie_pauzy = teraz - stav->cas_zaciatku_pauzy;

                // 2. K starému začiatku PRIČÍTAME túto pauzu (neprepisujeme ho!)
                stav->cas_zaciatku_hry += trvanie_pauzy;

                // Posunieme aj timeout pre neaktivitu
                stav->cas_posledneho_hraca += trvanie_pauzy;

                // 3. Nastavíme odpočítavanie
                stav->cas_obnovenia = time(NULL) + 3;

                printf("[SERVER] Pokračovanie... Pauza trvala: %ld s\n", trvanie_pauzy);
            }

            if (stav->vstup == SMER_KONIEC) {
                printf("[SERVER] Hráč definitívne odišiel\n");

                stav->hrac_pripojeny = false;
                stav->hra_spustena = false;
                stav->dlzka_hada = 0;
                stav->cas_posledneho_hraca = time(NULL);

                stav->hra_pozastavena = false; // Aby sa dalo hneď pripojiť
                stav->cas_obnovenia = 0;
                stav->skore = 0;
                stav->kolo_skoncilo = false;

            } else if (stav->vstup != SMER_NONE) {
                // Smer meníme iba ak to NIE JE pauza alebo pokračovanie
                if (stav->vstup != SMER_PAUZA && stav->vstup != SMER_POKRACUJ) {
                    if (!je_opacny(stav->aktualny_smer, stav->vstup)) {
                        stav->aktualny_smer = stav->vstup;
                    }
                }
            }
            stav->novy_vstup = false;
        }

        // 1. STAV: PAUZA (MENU)
        // Had stojí, čas stojí (virtuálne), klient kreslí menu
        if (stav->hra_pozastavena) {
            stav->tick++; // Nutné, aby klient vedel reagovať na vstup v menu
            pthread_cond_broadcast(&stav->cond_tick);
            pthread_mutex_unlock(&stav->mutex);
            continue;
        }

        // 2. STAV: ODPOČÍTAVANIE (3 sekundy pred štartom)
        // Had stojí, ale vidíme ho na mape
        if (stav->cas_obnovenia != 0) {
            if (time(NULL) < stav->cas_obnovenia) {
                // Ešte neprešli 3 sekundy
                // NEHÝBEME HADOM (žiadne posun_telo, posun_hlavu)

                stav->tick++; // ALE POSIELAME TICKY! Aby klient videl mapu a odpočítavanie
                pthread_cond_broadcast(&stav->cond_tick);
                pthread_mutex_unlock(&stav->mutex);
                continue;
            } else {
                // 3 sekundy prešli -> ideme hrať
                stav->cas_obnovenia = 0;
                stav->hra_spustena = true;
                // Prirátame 3 sekundy k času začiatku, aby sa čas strávený
                // čakaním nezapočítal do doby prežitia.
                stav->cas_zaciatku_hry += 3;
                stav->cas_posledneho_hraca += 3;
                // Had pokračuje v smere, v ktorom bol naposledy (stav->aktualny_smer sa nezmenil)
                printf("[SERVER] Odpočítavanie skončilo, had sa hýbe.\n");
            }
        }

        // ak kolo skončilo alebo had neexistuje alebo hra nie je spustená -> NEHÝB
        if (stav->kolo_skoncilo || stav->dlzka_hada == 0 || !stav->hra_spustena) {
            stav->tick++;

            pthread_cond_broadcast(&stav->cond_tick);
            pthread_mutex_unlock(&stav->mutex);
            continue;
        }

        posun_telo(stav);
        posun_hlavu(stav);

        // kolízia s prekážkou (iba v režime s prekážkami)
        if (stav->rezim == SVET_S_PREKAZKAMI && je_prekazka(stav, stav->had[0].x, stav->had[0].y)) {

            printf("[SERVER] NÁRAZ DO PREKÁŽKY! GAME OVER\n");

            if (stav->rezim_ukoncenia == UKONCENIE_CASOVE) {
                stav->server_bezi = false;
                pthread_cond_broadcast(&stav->cond_tick);
                pthread_mutex_unlock(&stav->mutex);
                break;
            }

            stav->kolo_skoncilo = true;
            stav->hrac_pripojeny = false;
            stav->hra_spustena = false;
            stav->dlzka_hada = 0;   // had zmizne
            stav->cas_posledneho_hraca = time(NULL);

            pthread_cond_broadcast(&stav->cond_tick);
            pthread_mutex_unlock(&stav->mutex);
            continue;
        }

        if (kolizia_so_sebou(stav)) {
            printf("[SERVER] KOLÍZIA SO SEBOU! GAME OVER\n");

            if (stav->rezim_ukoncenia == UKONCENIE_CASOVE) {
                stav->server_bezi = false;
                pthread_cond_broadcast(&stav->cond_tick);
                pthread_mutex_unlock(&stav->mutex);
                break;
            }

            stav->kolo_skoncilo = true;
            stav->hrac_pripojeny = false;
            stav->hra_spustena = false;
            stav->dlzka_hada = 0;   // had zmizne
            stav->cas_posledneho_hraca = time(NULL);

            pthread_cond_broadcast(&stav->cond_tick);
            pthread_mutex_unlock(&stav->mutex);
            continue;
        }

        // zjedol ovocie?
        if (stav->had[0].x == stav->ovocie.x && stav->had[0].y == stav->ovocie.y) {

            if (stav->dlzka_hada < MAX_DLZKA_HADA) {
                stav->had[stav->dlzka_hada] = stav->had[stav->dlzka_hada - 1]; // skopíruj chvost
                stav->dlzka_hada++;
            }
            stav->skore += 10;

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
