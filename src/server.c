#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/logic.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MIN_PLAYERS 4

typedef struct {
    int sockets[MAX_PLAYERS];
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
    int fd;
    GameData *game;
    int player_id;
} ThreadArgs;

void broadcast(GameData *game, GamePacket *pkt) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->sockets[i] != -1) {
            game->ipc.send_packet(game->sockets[i], pkt);
        }
    }
}

void start_new_round(GameData *game) {
    if (game->round_active) return;

    game->round_active = 1;
    game->current_player = 0;
    game->current_bet_count = 0;
    game->current_bet_value = -1;

    rozdaj_karty_vsetkym(game->player_cards, game->sockets, game->ipc, game->lives, game->current_player);

    GamePacket update_pkt = {0};
    update_pkt.MessageType = MSG_UPDATE;
    strcpy(update_pkt.text, "Nové kolo začalo! Na ťahu je Hráč 1.");
    update_pkt.current_player_id = 1;
    update_pkt.count = 0;
    update_pkt.card_value = -1;
    memcpy(update_pkt.lives, game->lives, sizeof(game->lives));
    broadcast(game, &update_pkt);
}

void evaluate_liar(GameData *game, int caller_id) {
    int called_value = game->current_bet_value;
    int bet_count = game->current_bet_count;

    int total_count = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (game->lives[p] > 0) {
            for (int c = 0; c < game->lives[p]; c++) {
                if (game->player_cards[p][c] == called_value) {
                    total_count++;
                }
            }
        }
    }

    bool liar_succeeds = (total_count < bet_count);
    int loser_id = liar_succeeds ? game->current_player : caller_id;
    
    game->lives[loser_id]--;

    GamePacket result_pkt = {0};
    result_pkt.MessageType = MSG_UPDATE;
    if (liar_succeeds) {
        sprintf(result_pkt.text, "Klamár uspel! Bolo len %d. Hráč %d stráca život.", total_count, loser_id + 1);
    } else {
        sprintf(result_pkt.text, "Klamár neuspel! Bolo %d alebo viac. Hráč %d stráca život.", bet_count, loser_id + 1);
    }
    memcpy(result_pkt.lives, game->lives, sizeof(game->lives));
    broadcast(game, &result_pkt);

    // Check for game over
    int alive_count = 0;
    int winner_id = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (game->lives[i] > 0) {
            alive_count++;
            winner_id = i;
        }
    }

    if (alive_count <= 1) {
        GamePacket game_over_pkt = {0};
        game_over_pkt.MessageType = MSG_GAME_OVER;
        sprintf(game_over_pkt.text, "Hra skončila! Víťaz je Hráč %d.", winner_id + 1);
        broadcast(game, &game_over_pkt);
        game->round_active = 0;
    } else {
        game->round_active = 0;
        start_new_round(game);
    }
}

void* handle_client(void* arg) {
    ThreadArgs *ta = (ThreadArgs*)arg;
    int my_id = -1;

    pthread_mutex_lock(&ta->game->mutex);
    if (ta->game->connected_players_count < MAX_PLAYERS) {
        ta->game->connected_players_count++;
        my_id = ta->game->connected_players_count;
        ta->game->sockets[my_id - 1] = ta->fd;
        ta->game->lives[my_id - 1] = INITIAL_LIVES;
        ta->player_id = my_id;
    }
    pthread_mutex_unlock(&ta->game->mutex);

    if (my_id == -1) {
        ta->game->ipc.close_conn(ta->fd);
        free(ta);
        return NULL;
    }

    GamePacket welcome = {0};
    welcome.MessageType = MSG_WELCOME;
    welcome.player_id = my_id;
    sprintf(welcome.text, "Vitaj! Si Hráč %d.", my_id);
    memcpy(welcome.lives, ta->game->lives, sizeof(welcome.lives));
    ta->game->ipc.send_packet(ta->fd, &welcome);

    char wait_text[100];
    sprintf(wait_text, "Čakáme na ďalších hráčov... (%d/%d)", ta->game->connected_players_count, MIN_PLAYERS);
    GamePacket wait_pkt = {0};
    wait_pkt.MessageType = MSG_UPDATE;
    strcpy(wait_pkt.text, wait_text);
    broadcast(ta->game, &wait_pkt);

    pthread_mutex_lock(&ta->game->mutex);
    if (ta->game->connected_players_count >= MIN_PLAYERS && ta->game->round_active == 0) {
        start_new_round(ta->game);
    }
    pthread_mutex_unlock(&ta->game->mutex);

    GamePacket pkt;
    while (1) {
        int res = ta->game->ipc.receive_packet(ta->fd, &pkt);

        if (res <= 0) {
            break;
        }

        if (pkt.MessageType == MSG_JOIN) {
            continue;
        }

        pthread_mutex_lock(&ta->game->mutex);

        if (!ta->game->round_active) {
            pthread_mutex_unlock(&ta->game->mutex);
            continue;
        }

        int current_id = ta->game->current_player + 1;

        if (pkt.MessageType == MSG_BET) {
            if (my_id != current_id) {
                pthread_mutex_unlock(&ta->game->mutex);
                continue;
            }

            int new_count = pkt.count;
            int new_value = pkt.card_value;

            bool higher = false;
            if (ta->game->current_bet_count == 0 && ta->game->current_bet_value == -1) {
                higher = true;
            } else if (new_count > ta->game->current_bet_count) {
                higher = true;
            } else if (new_count == ta->game->current_bet_count && new_value > ta->game->current_bet_value) {
                higher = true;
            }

            if (higher) {
                ta->game->current_bet_count = new_count;
                ta->game->current_bet_value = new_value;

                const char* names[] = {"Q", "K", "A", "J"};
                char text[100];
                sprintf(text, "Hráč %d staví: %dx %s", my_id, new_count, names[new_value]);

                GamePacket up = {0};
                up.MessageType = MSG_UPDATE;
                strcpy(up.text, text);
                up.count = new_count;
                up.card_value = new_value;
                up.current_player_id = my_id;
                memcpy(up.lives, ta->game->lives, sizeof(up.lives));
                broadcast(ta->game, &up);

                do {
                    ta->game->current_player = (ta->game->current_player + 1) % MAX_PLAYERS;
                } while (ta->game->lives[ta->game->current_player] <= 0);

                GamePacket turn = {0};
                turn.MessageType = MSG_UPDATE;
                sprintf(turn.text, "Na ťahu je Hráč %d", ta->game->current_player + 1);
                turn.current_player_id = ta->game->current_player + 1;
                turn.count = new_count;
                turn.card_value = new_value;
                memcpy(turn.lives, ta->game->lives, sizeof(turn.lives));
                broadcast(ta->game, &turn);
            } else {
                GamePacket err = {0};
                err.MessageType = MSG_UPDATE;
                strcpy(err.text, "Tvoja stávka nie je vyššia – skús znova!");
                memcpy(err.lives, ta->game->lives, sizeof(err.lives));
                err.current_player_id = current_id;
                err.count = ta->game->current_bet_count;
                err.card_value = ta->game->current_bet_value;
                ta->game->ipc.send_packet(ta->fd, &err);
            }

            pthread_mutex_unlock(&ta->game->mutex);
            continue;
        }

        if (pkt.MessageType == MSG_LIAR) {
            int previous_player = (ta->game->current_player - 1 + MAX_PLAYERS) % MAX_PLAYERS;
            int last_bettor_id = previous_player + 1;

            if (my_id == last_bettor_id || ta->game->current_bet_count == 0) {
                GamePacket err = {0};
                err.MessageType = MSG_UPDATE;
                strcpy(err.text, "Nemôžeš volať KLAMÁR! – buď si práve stávkoval alebo nie je stávka.");
                memcpy(err.lives, ta->game->lives, sizeof(err.lives));
                err.current_player_id = current_id;
                err.count = ta->game->current_bet_count;
                err.card_value = ta->game->current_bet_value;
                ta->game->ipc.send_packet(ta->fd, &err);
                pthread_mutex_unlock(&ta->game->mutex);
                continue;
            }

            pthread_mutex_unlock(&ta->game->mutex);
            evaluate_liar(ta->game, my_id);
            continue;
        }

        pthread_mutex_unlock(&ta->game->mutex);
    }

    pthread_mutex_lock(&ta->game->mutex);
    ta->game->sockets[my_id - 1] = -1;
    ta->game->lives[my_id - 1] = 0;
    ta->game->connected_players_count--;

    char disc_text[100];
    sprintf(disc_text, "Hráč %d sa odpojil.", my_id);
    GamePacket disc_pkt = {0};
    disc_pkt.MessageType = MSG_UPDATE;
    strcpy(disc_pkt.text, disc_text);
    memcpy(disc_pkt.lives, ta->game->lives, sizeof(disc_pkt.lives));
    broadcast(ta->game, &disc_pkt);

    pthread_mutex_unlock(&ta->game->mutex);

    ta->game->ipc.close_conn(ta->fd);
    free(ta);
    return NULL;
}

int main() {
    GameData game = {0};
    game.ipc = get_socket_interface();
    pthread_mutex_init(&game.mutex, NULL);
    for (int i = 0; i < MAX_PLAYERS; i++) game.sockets[i] = -1;

    int s_fd = game.ipc.init_server();
    if (s_fd < 0) return 1;

    printf("Server beží na porte %d\n", PORT);

    while (1) {
        int c_fd = accept(s_fd, NULL, NULL);
        if (c_fd < 0) continue;

        ThreadArgs *ta = malloc(sizeof(ThreadArgs));
        ta->fd = c_fd;
        ta->game = &game;

        pthread_t t;
        pthread_create(&t, NULL, handle_client, ta);
        pthread_detach(t);
    }
    return 0;
}