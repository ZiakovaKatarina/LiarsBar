#include "../include/client_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SERVER_PATH "./server"

void client_state_init(ClientState* state, IPC_Interface ipc) {
    memset(state, 0, sizeof(ClientState));
    state->ipc = ipc;
    state->fd = -1;
    state->is_running = true;
    state->current_bet_value = -1;
    pthread_mutex_init(&state->mutex, NULL);

    for (int i = 0; i < MAX_LIVES; i++) state->my_cards[i] = -1;
}

void client_state_destroy(ClientState* state) {
    client_disconnect(state);
    pthread_mutex_destroy(&state->mutex);
}

bool client_connect(ClientState* state, const char* address, bool auto_start_server) {
    state->is_running = true;
    state->game_over = false;
    state->round_started = false;
    state->message_is_error = false;
    state->last_message[0] = '\0';

    memset(state->lives, 0, sizeof(state->lives));
    for (int i = 0; i < MAX_LIVES; i++) {
        state->my_cards[i] = -1;
    }

    state->fd = state->ipc.init_client(address);

    if (state->fd < 0 && auto_start_server) {
        printf("⚠️  Server not found. Attempting to start server...\n");
        system(SERVER_PATH " &");
        sleep(1);

        state->fd = state->ipc.init_client(address);
    }

    return state->fd >= 0;
}

void client_disconnect(ClientState* state) {
    if (state->fd >= 0) {
        state->ipc.close_conn(state->fd);
        state->fd = -1;
    }
}

void client_send_join(ClientState* state, int game_id, int lives, int players) {
    GamePacket pkt = {.type = MSG_JOIN,
                      .game_id = game_id,
                      .count = lives,
                      .player_id = players};
    state->ipc.send_packet(state->fd, &pkt);
}

void client_send_bet(ClientState* state, int count, int card_value) {
    GamePacket pkt = {.type = MSG_BET,
                      .count = count,
                      .card_value = card_value};
    state->ipc.send_packet(state->fd, &pkt);
}

void client_send_liar(ClientState* state) {
    GamePacket pkt = {.type = MSG_LIAR};
    state->ipc.send_packet(state->fd, &pkt);
}

void client_send_quit(ClientState* state) {
    GamePacket pkt = {.type = MSG_QUIT};
    state->ipc.send_packet(state->fd, &pkt);

    pthread_mutex_lock(&state->mutex);
    state->is_running = false;
    pthread_mutex_unlock(&state->mutex);
}

bool client_process_packet(ClientState* state, GamePacket* pkt) {
    pthread_mutex_lock(&state->mutex);

    if (strlen(pkt->text) > 0) {
        strncpy(state->last_message, pkt->text, sizeof(state->last_message) - 1);
        state->message_is_error = (strstr(pkt->text, "❌") != NULL);
    }

    switch (pkt->type) {
        case MSG_WELCOME:
            state->player_id = pkt->player_id;
            state->game_id = pkt->game_id;
            memcpy(state->lives, pkt->lives, sizeof(state->lives));
            printf("[SERVER]: 🎮 Welcome! You are Player %d in game %d.\n", pkt->player_id, pkt->game_id);
            break;
        case MSG_START_ROUND:
            state->round_started = true;
            state->current_bet_count = 0;
            state->current_bet_value = -1;
            state->current_player_id = pkt->current_player_id;
            memcpy(state->my_cards, pkt->my_cards, sizeof(state->my_cards));
            memcpy(state->lives, pkt->lives, sizeof(state->lives));
            break;
        case MSG_UPDATE:
            if (pkt->count > 0) {
                state->current_bet_count = pkt->count;
                state->current_bet_value = pkt->card_value;
            }

            if (pkt->current_player_id > 0) {
                state->current_player_id = pkt->current_player_id;
            }

            memcpy(state->lives, pkt->lives, sizeof(state->lives));
            break;
        case MSG_GAME_OVER:
            state->game_over = true;
            state->is_running = false;

            printf(BOLD YELLOW "\n╔════════════════════════════════╗\n");
            printf("║        🏆 GAME OVER 🏆         ║\n");
            printf("╚════════════════════════════════╝\n");
            break;
        default:
            break;
    }

    pthread_mutex_unlock(&state->mutex);
    return true;
}