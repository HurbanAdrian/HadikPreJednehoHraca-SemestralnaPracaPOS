#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include "game_logic.h"

void cleanup_and_exit(int signo) {
    printf("\n[SERVER] Signál %d -> Upratujem...\n", signo);
    shm_unlink(SHM_NAME);
    exit(0);
}

int main(int argc, char** argv) {
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);

    RezimSveta svet = SVET_BEZ_PREKAZOK;
    RezimUkoncenia ukoncenie = UKONCENIE_STANDARDNE;
    int limit = 0;
    int sirka = 40;
    int vyska = 20;

    if (argc >= 2) { svet = atoi(argv[1]) ? SVET_S_PREKAZKAMI : SVET_BEZ_PREKAZOK; }
    if (argc >= 3) { ukoncenie = atoi(argv[2]) ? UKONCENIE_CASOVE : UKONCENIE_STANDARDNE; }
    if (argc >= 4) { limit = atoi(argv[3]); }
    if (argc >= 5) { sirka = atoi(argv[4]); }
    if (argc >= 6) { vyska = atoi(argv[5]); }

    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(HernyStav)) == -1) {
        perror("ftruncate");
        return 1;
    }

    HernyStav* stav = mmap(NULL, sizeof(HernyStav), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (stav == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&stav->mutex, &mattr);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&stav->cond_tick, &cattr);
    pthread_cond_init(&stav->cond_vstup, &cattr);

    pthread_mutex_lock(&stav->mutex);
    inicializuj_hru(stav, svet, ukoncenie, sirka, vyska, limit);
    pthread_mutex_unlock(&stav->mutex);

    printf("[SERVER] Bežím. Rozmery: %dx%d.\n", stav->sirka, stav->vyska);

    bool bol_pripojeny = false;

    while (stav->server_bezi) {
        usleep(200000);

        pthread_mutex_lock(&stav->mutex);
        time_t teraz = time(NULL);

        // 1. Logika pripojenia hráča
        if (stav->hrac_pripojeny && !bol_pripojeny) {
            resetuj_kolo(stav);
            vygeneruj_ovocie(stav);
            stav->kolo_skoncilo = false;
            bol_pripojeny = true;
            //printf("[SERVER] Nový hráč -> Reset.\n");
        } else if (!stav->hrac_pripojeny) {
            bol_pripojeny = false;
        }

        // 2. Timeout neaktivity (ak nikto nehrá 10s)
        bool neaktivita = !stav->hrac_pripojeny && difftime(teraz, stav->cas_posledneho_hraca) >= 10;
        if (neaktivita && (stav->rezim_ukoncenia == UKONCENIE_STANDARDNE || stav->kolo_skoncilo)) {
            stav->server_bezi = false;
            //printf("[SERVER] Timeout neaktivity.\n");
        }

        // 3. Kontrola, či hra stojí (Pauza, Odpočet, Game Over)
        bool stoji = stav->hra_pozastavena || stav->cas_obnovenia != 0 || stav->kolo_skoncilo || !stav->hra_spustena;

        // Vstup od klienta spracujeme vždy (aby mohol dať pauzu/koniec)
        if (stav->novy_vstup) {
            if (stav->vstup == SMER_PAUZA) {
                stav->hra_pozastavena = true;
                stav->cas_zaciatku_pauzy = teraz;
            } else if (stav->vstup == SMER_POKRACUJ) {
                stav->hra_pozastavena = false;
                // Posunieme začiatok hry o dĺžku pauzy
                stav->cas_zaciatku_hry += (teraz - stav->cas_zaciatku_pauzy);
                // Nastavíme odpočet na 3 sekundy
                stav->cas_obnovenia = teraz + 3;
            } else if (stav->vstup == SMER_KONIEC) {
                stav->hrac_pripojeny = false;
                stav->hra_spustena = false;
                stav->dlzka_hada = 0;
                stav->cas_posledneho_hraca = teraz;
            } else {
                if (!stav->hra_spustena && !stav->kolo_skoncilo && stav->cas_obnovenia == 0) {
                    stav->hra_spustena = true;
                    stav->cas_zaciatku_hry = teraz;
                }
                spracuj_zmenu_smeru(stav);
            }
            stav->novy_vstup = false;
        }

        // Odpočet (3..2..1)
        if (stav->cas_obnovenia != 0 && teraz >= stav->cas_obnovenia) {
             stav->cas_obnovenia = 0; // Koniec odpočtu
             // Počas odpočtu hra stála 3 sekundy. Musíme ich pripočítať k času začiatku,
             // inak by sa tieto 3 sekundy rátali do limitu hry.
             stav->cas_zaciatku_hry += 3;
        }

        // 4. HLAVNÁ HERNÁ LOGIKA (Beží len keď hra nestojí)
        if (!stoji) {

            // A) Kontrola ČASOVÉHO LIMITU (presunutá SEM, aby nebežala cez pauzu)
            if (stav->rezim_ukoncenia == UKONCENIE_CASOVE) {
                if (difftime(teraz, stav->cas_zaciatku_hry) >= stav->limit_casu) {
                    stav->kolo_skoncilo = true;
                    stav->server_bezi = false;
                    stav->cas_posledneho_hraca = teraz; // <--- OPRAVA: Uložíme čas konca!
                    printf("[SERVER] Čas vypršal.\n");

                    // Musíme odomknúť a vyskočiť, inak by sme pokračovali v pohybe hada
                    pthread_cond_broadcast(&stav->cond_tick);
                    pthread_mutex_unlock(&stav->mutex);
                    break;
                }
            }

            // B) Pohyb a Kolízie
            spracuj_pohyb(stav);

            if (skontroluj_kolizie(stav)) {
                stav->kolo_skoncilo = true;
                stav->hra_spustena = false;
                stav->dlzka_hada = 0;
                stav->cas_posledneho_hraca = teraz; // Uložíme čas smrti
                printf("[SERVER] Kolízia -> Game Over.\n");
            }

            if (skontroluj_ovocie(stav)) {
                printf("[SERVER] Skóre: %d\n", stav->skore);
            }
        }

        if (!stav->server_bezi) {
            pthread_cond_broadcast(&stav->cond_tick);
            pthread_mutex_unlock(&stav->mutex);
            break;
        }

        stav->tick++;
        pthread_cond_broadcast(&stav->cond_tick);
        pthread_mutex_unlock(&stav->mutex);
    }

    //printf("[SERVER] Končím.\n");
    shm_unlink(SHM_NAME);
    return 0;
}