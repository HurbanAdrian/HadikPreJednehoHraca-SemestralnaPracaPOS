#include "ui.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>

static struct termios povodny_term;

void init_terminal(void) {
    tcgetattr(STDIN_FILENO, &povodny_term);
}

void cleanup_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &povodny_term);
}

void vypni_echo(void) {
    struct termios t = povodny_term;
    t.c_lflag &= ~(ECHO | ICANON);            // (ECHO | ICANON) = bitová maska ~(...) = „vypni tieto bity“ &= = aplikuj zmenu    Enter nie je potrebný & klávesy sa nevypisujú
    tcsetattr(STDIN_FILENO, TCSANOW, &t);       // uložíme pôvodný stav
}

void vycisti_obrazovku(void) {
    // ANSI sekvencia pre vymazanie obrazovky a presun kurzora hore
    printf("\033[H\033[J");
}

void vykresli_mapu(HernyStav* stav) {
    // Použijeme tvoju logiku s bufferom - je to prehľadné
    // Pozor: VLA (Variable Length Array) je vo C99 povolené
    char mapa[stav->vyska][stav->sirka];

    // 1. Vyplň prázdno
    for (int y = 0; y < stav->vyska; y++) {
        for (int x = 0; x < stav->sirka; x++) {
            mapa[y][x] = ZNAK_PRAZDNO;
        }
    }

    // 2. Prekážky
    if (stav->rezim == SVET_S_PREKAZKAMI) {
        for (int i = 0; i < stav->pocet_prekazok; i++) {
            int x = stav->prekazky[i].x;
            int y = stav->prekazky[i].y;
            if (x >= 0 && x < stav->sirka && y >= 0 && y < stav->vyska) {
                mapa[y][x] = ZNAK_STENA;
            }
        }
    }

    // 3. Ovocie
    if (stav->ovocie.x >= 0 && stav->ovocie.x < stav->sirka &&
        stav->ovocie.y >= 0 && stav->ovocie.y < stav->vyska) {
        mapa[stav->ovocie.y][stav->ovocie.x] = ZNAK_OVOCIE;
    }

    // 4. Had
    if (stav->dlzka_hada > 0) {
        // Telo
        for (int i = 1; i < stav->dlzka_hada; i++) {
             if (stav->had[i].x >= 0 && stav->had[i].x < stav->sirka &&
                 stav->had[i].y >= 0 && stav->had[i].y < stav->vyska) {
                mapa[stav->had[i].y][stav->had[i].x] = ZNAK_TELO;
             }
        }
        // Hlava
        if (stav->had[0].x >= 0 && stav->had[0].x < stav->sirka &&
            stav->had[0].y >= 0 && stav->had[0].y < stav->vyska) {
            mapa[stav->had[0].y][stav->had[0].x] = ZNAK_HLAVA;
        }
    }

    // 5. Vykreslenie (s farbičkami a rámčekom)

    // Horná stena
    printf(FARBA_MODRA "#");
    for (int x = 0; x < stav->sirka; x++) printf("#");
    printf("#\n" FARBA_RESET);

    // Vnútro
    for (int y = 0; y < stav->vyska; y++) {
        printf(FARBA_MODRA "#" FARBA_RESET); // Ľavá stena
        for (int x = 0; x < stav->sirka; x++) {
            char c = mapa[y][x];
            switch (c) {
                case ZNAK_OVOCIE: printf(FARBA_CERVENA "%c" FARBA_RESET, c); break;
                case ZNAK_HLAVA:
                case ZNAK_TELO:   printf(FARBA_ZELENA "%c" FARBA_RESET, c); break;
                case ZNAK_STENA:  printf(FARBA_MODRA "%c" FARBA_RESET, c); break;
                default:          printf(FARBA_BIELA "%c" FARBA_RESET, c); break;
            }
        }
        printf(FARBA_MODRA "#\n" FARBA_RESET); // Pravá stena
    }

    // Dolná stena
    printf(FARBA_MODRA "#");
    for (int x = 0; x < stav->sirka; x++) printf("#");
    printf("#\n" FARBA_RESET);
}

void vykresli_game_over(HernyStav* stav) {
    vycisti_obrazovku();
    printf(FARBA_CERVENA "GAME OVER\n" FARBA_RESET);

    int finalny_cas;
    if (stav->kolo_skoncilo) {
        finalny_cas = (int)difftime(stav->cas_posledneho_hraca, stav->cas_zaciatku_hry);
    } else {
        finalny_cas = (int)difftime(time(NULL), stav->cas_zaciatku_hry);
    }

    // Korekcia pre časový limit
    if (stav->rezim_ukoncenia == UKONCENIE_CASOVE && finalny_cas > stav->limit_casu) {
        finalny_cas = stav->limit_casu;
    }
    if (finalny_cas < 0) finalny_cas = 0;

    printf("Finálne skóre: " FARBA_ZELENA "%d" FARBA_RESET "\n", stav->skore);
    printf("Doba prežitia: " FARBA_MODRA  "%d s" FARBA_RESET "\n\n", finalny_cas);
    printf("Stlač Q pre odpojenie (potom spusti ./client znova pre novú hru)\n");
}

void vykresli_menu_pauza(void) {
    printf(FARBA_MODRA "=== HLAVNÉ MENU ===\n" FARBA_RESET);
    printf("Hra je pozastavená.\n\n");
    printf("Stlač " FARBA_ZELENA "'r'" FARBA_RESET " pre návrat do hry\n");
    printf("Stlač " FARBA_CERVENA "'q'" FARBA_RESET " pre úplný koniec\n");
}