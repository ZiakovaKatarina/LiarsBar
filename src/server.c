#include "../include/common.h"
#include "../include/ipc_interface.h"
#include <stdio.h>
#include <pthread.h>

typedef struct {
    int sockets[MAX_PLAYERS];
    int connected_players_count;
    pthread_mutex_t mutex;
    IPC_Interface *ipc;
} GameData;

/*
typedef struct { // struktura pre konkretne vlakno
    int fd;
    GameData * game;
} ThreadArgs;
 */

typedef struct {
    int fd;
    IPC_Interface ipc;
} ThreadArgs;

void* handle_client(void* arg) {
    ThreadArgs *ta = (ThreadArgs*)arg;
    GamePacket pkt;
    
    // Prijatie MSG_JOIN
    if (ta->ipc.receive_packet(ta->fd, &pkt) > 0) {
        printf("[SERVER] Hrac %d poslal spravu: %s\n", pkt.player_id, pkt.text);
        
        // Odpoved
        pkt.MessageType = MSG_WELCOME;
        strcpy(pkt.text, "Ahoj z interface!");
        ta->ipc.send_packet(ta->fd, &pkt);
    }
    
    ta->ipc.close_conn(ta->fd);
    free(ta);
    return NULL;
}

int main() {
    IPC_Interface socket_ipc = get_socket_interface();
    int s_fd = socket_ipc.init_server(8080);
    
    printf("Server caka na pripojenie...\n");
    
    while(1) {
        int c_fd = accept(s_fd, NULL, NULL);
        ThreadArgs *ta = malloc(sizeof(ThreadArgs));
        ta->fd = c_fd;
        ta->ipc = socket_ipc;
        
        pthread_t t;
        pthread_create(&t, NULL, handle_client, ta);
        pthread_detach(t);
    }
    return 0;
}