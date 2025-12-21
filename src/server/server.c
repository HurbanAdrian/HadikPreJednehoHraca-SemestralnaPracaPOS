#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>

#include "../common/common.h"

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

    // 5. inicializuj stav hry
    pthread_mutex_lock(&stav->mutex);
    stav->server_bezi = true;
    stav->tick = 0;
    pthread_mutex_unlock(&stav->mutex);

    printf("[SERVER] Bežím...\n");

    // 6. hlavný cyklus servera
    while (1) {
        usleep(200000); // 200 ms

        pthread_mutex_lock(&stav->mutex);
        stav->tick++;
        pthread_mutex_unlock(&stav->mutex);
    }
}
