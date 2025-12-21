// src/server.c
#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct {
    int sockets[MAX_PLAYERS];
    int connected_players_count;
    pthread_mutex_t mutex;
    IPC_Interface ipc;

    // Herný stav
    int lives[MAX_PLAYERS];
    int player_cards[MAX_PLAYERS][INITIAL_LIVES];  // karty každého hráča
    int current_player;             // index hráča na ťahu (0-3)
    int current_bet_count;          // aktuálna stávka: počet
    int current_bet_value;          // aktuálna stávka: hodnota (0=Q,1=K,2=A,3=J)
    int round_active;               // 1 = kolo beží
} GameData;

typedef struct {
    int fd;
    GameData *game;
    int player_id;  // pridáme, aby sme vedeli, kto je ktorý
} ThreadArgs;

// Pomocná funkcia na broadcast všetkým pripojeným
void broadcast(GameData *game, GamePacket *pkt) {
    pthread_mutex_lock(&game->mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->sockets[i] != -1) {
            game->ipc.send_packet(game->sockets[i], pkt);
        }
    }
    pthread_mutex_unlock(&game->mutex);
}

// Nová funkcia: spustenie nového kola
void start_new_round(GameData *game) {
    if (game->round_active) return;

    game->round_active = 1;
    game->current_player = 0;  // prvý pripojený hráč začína
    game->current_bet_count = 0;
    game->current_bet_value = -1;

    // Rozdaj karty a pošli všetkým MSG_START_ROUND
    rozdaj_karty_vsetkym(game->player_cards, game->sockets, game->ipc, game->lives, game->current_player);

    // Po rozdaní pošleme update všetkým (životy, kto na ťahu)
    GamePacket update_pkt = {0};
    update_pkt.MessageType = MSG_UPDATE;
    strcpy(update_pkt.text, "Nové kolo začalo! Prvý hráč je na ťahu.");
    update_pkt.current_player_id = game->current_player + 1;  // ID od 1
    memcpy(update_pkt.lives, game->lives, sizeof(game->lives));

    broadcast(game, &update_pkt);
}

void* handle_client(void* arg) {
    ThreadArgs *ta = (ThreadArgs*)arg;
    int my_id = -1;

    pthread_mutex_lock(&ta->game->mutex);

    if (ta->game->connected_players_count < MAX_PLAYERS) {
        ta->game->connected_players_count++;
        my_id = ta->game->connected_players_count;  // ID od 1
        int index = my_id - 1;
        ta->game->sockets[index] = ta->fd;
        ta->game->lives[index] = INITIAL_LIVES;
        ta->player_id = my_id;
    }

    pthread_mutex_unlock(&ta->game->mutex);

    if (my_id == -1) {
        printf("[SERVER] Server plný, odmietam pripojenie.\n");
        ta->game->ipc.close_conn(ta->fd);
        free(ta);
        return NULL;
    }

    printf("[SERVER] Hráč %d sa pripojil (FD %d)\n", my_id, ta->fd);

    // Pošli uvítaciu správu
    GamePacket welcome_pkt = {0};
    welcome_pkt.MessageType = MSG_WELCOME;
    welcome_pkt.player_id = my_id;
    sprintf(welcome_pkt.text, "Vitaj! Si Hráč %d.", my_id);
    ta->game->ipc.send_packet(ta->fd, &welcome_pkt);

    // Ak máme aspoň 2 hráčov, spusti hru
    pthread_mutex_lock(&ta->game->mutex);
    if (ta->game->connected_players_count >= 2 && ta->game->round_active == 0) {
        start_new_round(ta->game);
    }
    pthread_mutex_unlock(&ta->game->mutex);

    // Hlavný loop: čakáme na správy od klienta (zatiaľ len ignorujeme, neskôr pridáme BET a LIAR)
    GamePacket pkt;
    while (ta->game->ipc.receive_packet(ta->fd, &pkt) > 0) {
        // Tu neskôr spracujeme MSG_BET a MSG_LIAR
        // Zatiaľ len logujeme
        printf("[SERVER] Dostal som správu typu %d od hráča %d\n", pkt.MessageType, my_id);
    }

    // Odpojenie hráča
    pthread_mutex_lock(&ta->game->mutex);
    int index = my_id - 1;
    ta->game->sockets[index] = -1;
    ta->game->lives[index] = 0;
    ta->game->connected_players_count--;
    printf("[SERVER] Hráč %d sa odpojil.\n", my_id);
    pthread_mutex_unlock(&ta->game->mutex);

    ta->game->ipc.close_conn(ta->fd);
    free(ta);
    return NULL;
}

int main() {
    GameData game = {0};
    game.ipc = get_socket_interface();
    pthread_mutex_init(&game.mutex, NULL);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        game.sockets[i] = -1;
        game.lives[i] = 0;
    }
    game.current_bet_count = 0;
    game.current_bet_value = -1;
    game.round_active = 0;

    int s_fd = game.ipc.init_server();
    if (s_fd < 0) {
        perror("init_server");
        return 1;
    }

    printf("Server beží na porte %d a čaká na hráčov...\n", PORT);

    while (1) {
        int c_fd = accept(s_fd, NULL, NULL);
        if (c_fd < 0) continue;

        ThreadArgs *ta = malloc(sizeof(ThreadArgs));
        ta->fd = c_fd;
        ta->game = &game;
        ta->player_id = 0;

        pthread_t t;
        pthread_create(&t, NULL, handle_client, ta);
        pthread_detach(t);
    }

    pthread_mutex_destroy(&game.mutex);
    close(s_fd);
    return 0;
}