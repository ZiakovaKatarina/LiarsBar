// src/server.c
#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

typedef struct {
    int read_fds[MAX_PLAYERS];   // fd na čítanie od klienta
    int write_fds[MAX_PLAYERS];  // fd na písanie klientovi
    int connected_players_count;
    pthread_mutex_t mutex;
    IPC_Interface ipc;

    int lives[MAX_PLAYERS];
    int player_cards[MAX_PLAYERS][INITIAL_LIVES];
    int current_player;
    int current_bet_count;
    int current_bet_value;
    int round_active;
} GameData;

typedef struct {
    int fd;          // read_fd
    GameData *game;
    int player_id;
} ThreadArgs;

void broadcast(GameData *game, GamePacket *pkt) {
    pthread_mutex_lock(&game->mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->write_fds[i] != -1) {
            game->ipc.send_packet(game->write_fds[i], pkt);
        }
    }
    pthread_mutex_unlock(&game->mutex);
}

void start_new_round(GameData *game) {
    if (game->round_active) return;

    game->round_active = 1;
    game->current_player = 0;
    game->current_bet_count = 0;
    game->current_bet_value = -1;

    rozdaj_karty_vsetkym(game->player_cards, game->write_fds, game->ipc, game->lives, game->current_player);

    GamePacket update_pkt = {0};
    update_pkt.MessageType = MSG_UPDATE;
    strcpy(update_pkt.text, "Nové kolo začalo! Prvý hráč je na ťahu.");
    update_pkt.current_player_id = game->current_player + 1;
    memcpy(update_pkt.lives, game->lives, sizeof(game->lives));

    broadcast(game, &update_pkt);
}

void* handle_client(void* arg) {
    ThreadArgs *ta = (ThreadArgs*)arg;
    GameData *game = ta->game;
    int my_id = -1;
    int index = -1;

    pthread_mutex_lock(&game->mutex);
    if (game->connected_players_count >= MAX_PLAYERS) {
        pthread_mutex_unlock(&game->mutex);
        printf("[SERVER] Server plný.\n");
        game->ipc.close_conn(ta->fd);
        free(ta);
        return NULL;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->read_fds[i] == -1) {
            index = i;
            break;
        }
    }

    game->read_fds[index] = ta->fd;

    // Univerzálne získanie write_fd cez IPC
    game->write_fds[index] = game->ipc.get_write_fd(ta->fd);

    game->lives[index] = INITIAL_LIVES;
    game->connected_players_count++;
    my_id = index + 1;
    ta->player_id = my_id;

    pthread_mutex_unlock(&game->mutex);

    printf("[SERVER] Hráč %d sa pripojil (read_fd=%d, write_fd=%d)\n", my_id, ta->fd, game->write_fds[index]);

    // Welcome packet
    GamePacket welcome_pkt = {0};
    welcome_pkt.MessageType = MSG_WELCOME;
    welcome_pkt.player_id = my_id;
    sprintf(welcome_pkt.text, "Vitaj! Si Hráč %d.", my_id);

    game->ipc.send_packet(game->write_fds[index], &welcome_pkt);
usleep(200000);
    // Spusti hru pri 2 hráčoch
    pthread_mutex_lock(&game->mutex);
    if (game->connected_players_count >= 2 && game->round_active == 0) {
        start_new_round(game);
    }
    pthread_mutex_unlock(&game->mutex);

    GamePacket pkt;
    while (game->ipc.receive_packet(game->read_fds[index], &pkt) > 0) {
        printf("[SERVER] Správa typu %d od hráča %d\n", pkt.MessageType, my_id);
        // Tu spracuj pakety
    }

    pthread_mutex_lock(&game->mutex);
    game->read_fds[index] = -1;
    game->write_fds[index] = -1;
    game->lives[index] = 0;
    game->connected_players_count--;
    printf("[SERVER] Hráč %d sa odpojil.\n", my_id);
    pthread_mutex_unlock(&game->mutex);

    game->ipc.close_conn(ta->fd);
    free(ta);
    return NULL;
}

int main() {
    GameData game = {0};
    game.ipc = get_pipe_interface();  // alebo get_socket_interface() – prepínanie tu

    pthread_mutex_init(&game.mutex, NULL);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        game.read_fds[i] = -1;
        game.write_fds[i] = -1;
        game.lives[i] = 0;
    }

    int server_fd = game.ipc.init_server();
    if (server_fd < 0) return 1;

    printf("Server spustený a čaká na hráčov...\n");

    while (1) {
        int client_fd = game.ipc.accept_client(server_fd);
        if (client_fd < 0) {
            usleep(100000);
            continue;
        }

        ThreadArgs *ta = malloc(sizeof(ThreadArgs));
        if (!ta) continue;

        ta->fd = client_fd;
        ta->game = &game;

        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, ta);
        pthread_detach(thread);
    }
}