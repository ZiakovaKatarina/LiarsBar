#include "../include/common.h"
#include "../include/ipc_interface.h"
#include <stdio.h>
#include <pthread.h>

typedef struct ClientThreadArgs {
    int fd;
    IPC_Interface ipc;
} ClientThreadArgs;

void* receive_thread(void* arg) {
    ClientThreadArgs *args = (ClientThreadArgs*)arg;
    GamePacket pkt;

    // Používame lokálnu kópiu rozhrania (P11)
    while(args->ipc.receive_packet(args->fd, &pkt) > 0) {
        if (pkt.MessageType == MSG_WELCOME) {
            printf("\n[SERVER]: %s\n", pkt.text);
        }
        printf("Zadaj text správy: ");
        fflush(stdout);
    }

    free(args);
    return NULL;
}

int main() {
    IPC_Interface socket_ipc = get_socket_interface();

    int fd = socket_ipc.init_client("127.0.0.1", 8080);
    if (fd < 0) {
        printf("Nepodarilo sa pripojiť k serveru.\n");
        return -1;
    }

    printf("Pripojený k serveru. Posielam MSG_JOIN...\n");

    GamePacket join_pkt;
    join_pkt.MessageType = MSG_JOIN;
    join_pkt.player_id = 1;
    strcpy(join_pkt.text, "Ahoj, ja som hrac 1");
    socket_ipc.send_packet(fd, &join_pkt);

    ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
    args->fd = fd;
    args->ipc = socket_ipc;

    pthread_t t;
    pthread_create(&t, NULL, receive_thread, args);
    pthread_detach(t);

    char buf[256];
    while(fgets(buf, 256, stdin)) {\
        if (strncmp(buf, "exit", 4) == 0) break;
    }

    socket_ipc.close_conn(fd);
    return 0;
}