#include "../include/common.h"
#include <stdio.h>
#include <pthread.h>

void* funkcia(void * args) {
    printf("funguje");
    return NULL;
}

int main() {
    pthread_t thread;
    pthread_create(&thread, NULL, funkcia, NULL);
    pthread_join(thread, NULL);
    printf("Server started");
    return 0;
}