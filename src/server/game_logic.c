#include "game_logic.h"
#include <stdlib.h>
#include <stdio.h>

// --- PRIVÁTNE FUNKCIE ---

static bool je_na_hadovi(HernyStav* stav, int x, int y) {
    for (int i = 0; i < stav->dlzka_hada; i++) {
        if (stav->had[i].x == x && stav->had[i].y == y) {
            return true;
        }
    }
    return false;
}

static bool je_prekazka(HernyStav* stav, int x, int y) {
    for (int i = 0; i < stav->pocet_prekazok; i++) {
        if (stav->prekazky[i].x == x && stav->prekazky[i].y == y) {
            return true;
        }
    }
    return false;
}

static void wrap_pozicia(HernyStav* stav, Pozicia* p) {
    if (p->x < 0) {
        p->x = stav->sirka - 1;
    } else if (p->x >= stav->sirka) {
        p->x = 0;
    }

    if (p->y < 0) {
        p->y = stav->vyska - 1;
    } else if (p->y >= stav->vyska) {
        p->y = 0;
    }
}

static bool je_opacny(Smer a, Smer b) {
    return (a == SMER_HORE && b == SMER_DOLE) ||
           (a == SMER_DOLE && b == SMER_HORE) ||
           (a == SMER_VLAVO && b == SMER_VPRAVO) ||
           (a == SMER_VPRAVO && b == SMER_VLAVO);
}

// --- VEREJNÉ FUNKCIE ---

void generuj_prekazky(HernyStav* stav) {
    stav->pocet_prekazok = 0;
    if (stav->rezim != SVET_S_PREKAZKAMI) {
        return;
    }

    int plocha = stav->sirka * stav->vyska;
    int ciel = (plocha * 5) / 100;
    if (ciel > MAX_PREKAZKY) {
        ciel = MAX_PREKAZKY;
    }

    while (stav->pocet_prekazok < ciel) {
        int x = rand() % stav->sirka;
        int y = rand() % stav->vyska;

        // 1. neokrajové pole (zjednodušenie dosiahnuteľnosti)
        if (x == 0 || x == stav->sirka - 1 || y == 0 || y == stav->vyska - 1) {
            continue;
        }

        // 2. nesmie byť na hadovi
        if (je_na_hadovi(stav, x, y)) continue;

        // 3. nesmie byť ovocie
        if (stav->ovocie.x == x && stav->ovocie.y == y) continue;

        // 4. nesmie sa opakovať
        if (je_prekazka(stav, x, y)) continue;

        stav->prekazky[stav->pocet_prekazok].x = x;
        stav->prekazky[stav->pocet_prekazok].y = y;
        stav->pocet_prekazok++;
    }
}

void vygeneruj_ovocie(HernyStav* stav) {
    int x, y;
    do {
        x = rand() % stav->sirka;
        y = rand() % stav->vyska;
    } while (je_na_hadovi(stav, x, y) || je_prekazka(stav, x, y));

    stav->ovocie.x = x;
    stav->ovocie.y = y;
    //printf("[LOGIKA] Ovocie: %d,%d\n", x, y);
}

void inicializuj_hru(HernyStav* stav, RezimSveta svet, RezimUkoncenia ukoncenie, int w, int h, int limit) {
    stav->server_bezi = true;
    stav->hrac_pripojeny = false;
    stav->hra_spustena = false;
    stav->kolo_skoncilo = false;
    stav->hra_pozastavena = false;
    stav->tick = 0;
    stav->skore = 0;

    stav->rezim = svet;
    stav->rezim_ukoncenia = ukoncenie;
    stav->limit_casu = limit;

    stav->sirka = (w > 0) ? w : 20;
    stav->vyska = (h > 0) ? h : 10;

    if (stav->sirka > MAX_MAP_WIDTH) {
        stav->sirka = MAX_MAP_WIDTH;
    }
    if (stav->vyska > MAX_MAP_HEIGHT) {
        stav->vyska = MAX_MAP_HEIGHT;
    }

    resetuj_kolo(stav);
    generuj_prekazky(stav);
    vygeneruj_ovocie(stav);

    stav->cas_zaciatku_hry = time(NULL);
    stav->cas_posledneho_hraca = time(NULL);
}

void resetuj_kolo(HernyStav* stav) {
    stav->dlzka_hada = 5;
    stav->aktualny_smer = SMER_VPRAVO;
    stav->hra_spustena = false;
    stav->kolo_skoncilo = false;
    stav->hra_pozastavena = false;
    stav->cas_obnovenia = 0;
    stav->skore = 0;

    for (int i = 0; i < stav->dlzka_hada; i++) {
        stav->had[i].x = (stav->sirka / 2) - i;
        stav->had[i].y = stav->vyska / 2;
    }
}

void spracuj_zmenu_smeru(HernyStav* stav) {
    if (stav->vstup != SMER_NONE && stav->vstup != SMER_PAUZA && stav->vstup != SMER_POKRACUJ) {
        if (!je_opacny(stav->aktualny_smer, stav->vstup)) {
            stav->aktualny_smer = stav->vstup;
        }
    }
}

void spracuj_pohyb(HernyStav* stav) {
    // najskor telo potom hlava
    for (int i = stav->dlzka_hada - 1; i > 0; i--) {
        stav->had[i] = stav->had[i - 1];
    }

    switch (stav->aktualny_smer) {
        case SMER_HORE:   stav->had[0].y--; break;
        case SMER_DOLE:   stav->had[0].y++; break;
        case SMER_VLAVO:  stav->had[0].x--; break;
        case SMER_VPRAVO: stav->had[0].x++; break;
        default: break;
    }
    wrap_pozicia(stav, &stav->had[0]);
}

bool skontroluj_kolizie(HernyStav* stav) {
    // 1. Telo
    for (int i = 1; i < stav->dlzka_hada; i++) {
        if (stav->had[0].x == stav->had[i].x && stav->had[0].y == stav->had[i].y) {
            return true;
        }
    }
    // 2. Prekážky
    if (stav->rezim == SVET_S_PREKAZKAMI) {
        if (je_prekazka(stav, stav->had[0].x, stav->had[0].y)) {
            return true;
        }
    }
    return false;
}

bool skontroluj_ovocie(HernyStav* stav) {
    if (stav->had[0].x == stav->ovocie.x && stav->had[0].y == stav->ovocie.y) {
        if (stav->dlzka_hada < MAX_DLZKA_HADA) {
            stav->had[stav->dlzka_hada] = stav->had[stav->dlzka_hada - 1];
            stav->dlzka_hada++;
        }
        stav->skore += 10;
        vygeneruj_ovocie(stav);
        return true;
    }
    return false;
}

const char* smer_na_text(Smer s) {
    switch (s) {
        case SMER_HORE: return "HORE";
        case SMER_DOLE: return "DOLE";
        case SMER_VLAVO: return "VLAVO";
        case SMER_VPRAVO: return "VPRAVO";
        default: return "-";
    }
}