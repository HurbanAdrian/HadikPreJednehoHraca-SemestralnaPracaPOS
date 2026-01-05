#ifndef UI_H
#define UI_H

#include "common.h"
#include <termios.h>

#define FARBA_RESET   "\033[0m"
#define FARBA_CERVENA "\033[31m"
#define FARBA_ZELENA  "\033[32m"
#define FARBA_MODRA   "\033[34m"
#define FARBA_BIELA   "\033[37m"

void init_terminal(void);
void cleanup_terminal(void);
void vypni_echo(void);
void vycisti_obrazovku(void);

void vykresli_mapu(HernyStav* stav);
void vykresli_game_over(HernyStav* stav);
void vykresli_menu_pauza(void);

#endif