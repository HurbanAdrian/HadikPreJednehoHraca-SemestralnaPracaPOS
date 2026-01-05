#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include "common.h"

void inicializuj_hru(HernyStav* stav, RezimSveta svet, RezimUkoncenia ukoncenie, int w, int h, int limit);
void resetuj_kolo(HernyStav* stav);

void spracuj_pohyb(HernyStav* stav);
void spracuj_zmenu_smeru(HernyStav* stav);
bool skontroluj_kolizie(HernyStav* stav);
bool skontroluj_ovocie(HernyStav* stav);

void vygeneruj_ovocie(HernyStav* stav);
void generuj_prekazky(HernyStav* stav);

const char* smer_na_text(Smer s);

#endif