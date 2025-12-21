#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#include "../common/common.h"

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
    while (1) {
        pthread_mutex_lock(&stav->mutex);
        unsigned long t = stav->tick;
        pthread_mutex_unlock(&stav->mutex);

        printf("Tick: %lu\n", t);
        usleep(300000); // 300 ms
    }
}
