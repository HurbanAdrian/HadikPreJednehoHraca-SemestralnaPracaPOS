#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#include "../common/common.h"

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

int main() {
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

    // 3. čítaj tick zo servera
    unsigned long posledny_tick = 0;

    printf("[CLIENT] Ovládanie: w a s d, q = koniec\n");

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

}
