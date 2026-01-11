#include "../include/server_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void game_init(ServerGame* game, int id, int max_players, int initial_lives,
               IPC_Interface ipc) {
    memset(game, 0, sizeof(ServerGame));
    game->game_id = id;
    game->max_players = max_players;
    game->initial_lives = initial_lives;
    game->is_running = true;
    game->current_bet_value = -1;
    game->last_bettor_idx = -1;
    game->current_player_idx = -1;
    game->ipc = ipc;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        game->players[i].fd = -1;
        game->players[i].is_active = false;
    }
    pthread_mutex_init(&game->mutex, NULL);
}

void game_destroy(ServerGame* game) { pthread_mutex_destroy(&game->mutex); }

static int next_alive_player(ServerGame* game, int start_idx) {
    int idx = start_idx;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        idx = (idx + 1) % game->max_players;
        if (game->players[idx].fd != -1 && game->players[idx].lives > 0) {
            return idx;
        }
    }
    return -1;
}

static void deal_cards(ServerGame* game) {
    int deck[20];
    int k = 0;
    for (int i = 0; i < 6; i++) {
        deck[k++] = CARD_QUEEN;
        deck[k++] = CARD_KING;
        deck[k++] = CARD_ACE;
    }
    deck[18] = CARD_JOKER;
    deck[19] = CARD_JOKER;

    for (int i = 19; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }

    int card_idx = 0;
    for (int i = 0; i < game->max_players; i++) {
        ServerPlayer* p = &game->players[i];
        if (p->fd != -1 && p->lives > 0) {
            player_set_hand(p, &deck[card_idx], p->lives);
            card_idx += p->lives;
        }
    }
}

int game_add_player(ServerGame* game, int fd, int lives) {
    pthread_mutex_lock(&game->mutex);

    if (game->connected_count >= game->max_players) {
        pthread_mutex_unlock(&game->mutex);
        return -1;
    }

    int idx = game->connected_count;
    player_init(&game->players[idx], idx + 1, fd, lives, game->ipc);
    game->connected_count++;

    printf("Game %d: Player %d joined.\n", game->game_id, idx + 1);

    if (game->connected_count == game->max_players) {
        game_start_round(game);
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Waiting for players (%d/%d)...",
                 game->connected_count, game->max_players);
        game_broadcast_update(game, msg);
    }

    pthread_mutex_unlock(&game->mutex);
    return idx;
}

void game_remove_player(ServerGame* game, int player_idx) {
    ServerPlayer* p = &game->players[player_idx];
    p->fd = -1;
    p->lives = 0;
    p->is_active = false;

    char msg[64];
    snprintf(msg, sizeof(msg), "Player %d disconnected.", p->id);
    game_broadcast_update(game, msg);
}

void game_start_round(ServerGame* game) {
    game->round_active = 1;
    game->current_bet_count = 0;
    game->current_bet_value = -1;
    game->last_bettor_idx = -1;

    if (game->current_player_idx == -1) game->current_player_idx = 0;

    if (game->players[game->current_player_idx].lives <= 0) {
        game->current_player_idx =
            next_alive_player(game, game->current_player_idx);
    }

    deal_cards(game);

    for (int i = 0; i < game->max_players; i++) {
        ServerPlayer* p = &game->players[i];
        if (p->fd != -1) {
            GamePacket pkt = {0};
            pkt.type = MSG_START_ROUND;
            pkt.current_player_id = game->players[game->current_player_idx].id;

            for (int c = 0; c < MAX_LIVES; c++) pkt.my_cards[c] = p->cards[c];
            for (int j = 0; j < MAX_PLAYERS; j++)
                pkt.lives[j] = game->players[j].lives;

            player_send_packet(p, &pkt);
        }
    }
    printf("Game %d: New round started.\n", game->game_id);
}

void game_process_bet(ServerGame* game, int player_idx, int count, int value) {
    ServerPlayer* p = &game->players[player_idx];

    if (player_idx != game->current_player_idx) {
        player_send_error(p, "Not your turn!");
        return;
    }

    bool valid = false;
    if (game->current_bet_value == -1)
        valid = true;
    else if (count > game->current_bet_count)
        valid = true;
    else if (count == game->current_bet_count &&
             value > game->current_bet_value)
        valid = true;

    if (!valid) {
        player_send_error(p, "Bet must be higher!");
        return;
    }

    game->current_bet_count = count;
    game->current_bet_value = value;
    game->last_bettor_idx = player_idx;

    game->current_player_idx =
        next_alive_player(game, game->current_player_idx);

    GamePacket pkt = {0};
    pkt.type = MSG_UPDATE;
    pkt.count = count;
    pkt.card_value = value;
    pkt.current_player_id = game->players[game->current_player_idx].id;
    for (int i = 0; i < MAX_PLAYERS; i++) pkt.lives[i] = game->players[i].lives;

    const char* card_names[] = {"Q", "K", "A", "J"};
    snprintf(pkt.text, sizeof(pkt.text), "Player %d bets: %d x %s", p->id,
             count, card_names[value]);

    game_broadcast(game, &pkt);
}

void game_process_liar(ServerGame* game, int caller_idx) {
    ServerPlayer* p = &game->players[caller_idx];

    if (caller_idx != game->current_player_idx) {
        player_send_error(p, "Not your turn!");
        return;
    }

    if (game->current_bet_value == -1) {
        player_send_error(p,
                          "❌ Cannot call Liar on empty table! You must bet.");
        return;
    }

    int actual_count = 0;
    for (int i = 0; i < game->max_players; i++) {
        if (game->players[i].lives > 0) {
            actual_count +=
                player_count_card(&game->players[i], game->current_bet_value);
        }
    }

    bool liar_success = (actual_count < game->current_bet_count);

    int loser_idx = liar_success ? game->last_bettor_idx : caller_idx;

    ServerPlayer* loser = &game->players[loser_idx];
    player_lose_life(loser);

    GamePacket pkt = {0};
    pkt.type = MSG_UPDATE;

    if (liar_success) {
        snprintf(pkt.text, sizeof(pkt.text),
                 "✅ Liar CORRECT! Actual: %d. Player %d loses life.",
                 actual_count, loser->id);
    } else {
        snprintf(pkt.text, sizeof(pkt.text),
                 "❌ Liar WRONG! Actual: %d. Player %d loses life.",
                 actual_count, loser->id);
    }

    for (int i = 0; i < MAX_PLAYERS; i++) pkt.lives[i] = game->players[i].lives;
    game_broadcast(game, &pkt);

    int alive_count = 0;
    int winner_id = -1;
    for (int i = 0; i < game->max_players; i++) {
        if (game->players[i].lives > 0) {
            alive_count++;
            winner_id = game->players[i].id;
        }
    }

    if (alive_count <= 1) {
        GamePacket end_pkt = {.type = MSG_GAME_OVER};
        if (winner_id != -1 && alive_count == 1) {
            snprintf(end_pkt.text, sizeof(end_pkt.text), "🏆 Player %d WINS!",
                     winner_id);
        } else {
            strcpy(end_pkt.text, "Everyone died. Draw.");
        }
        game_broadcast(game, &end_pkt);
        game->round_active = 0;
    } else {
        if (loser->lives > 0) {
            game->current_player_idx = loser_idx;
        } else {
            game->current_player_idx = next_alive_player(game, loser_idx);
        }

        game_start_round(game);
    }
}

void game_broadcast(ServerGame* game, GamePacket* pkt) {
    pkt->game_id = game->game_id;
    for (int i = 0; i < game->max_players; i++) {
        player_send_packet(&game->players[i], pkt);
    }
}

void game_broadcast_update(ServerGame* game, const char* msg) {
    GamePacket pkt = {0};
    pkt.type = MSG_UPDATE;
    strncpy(pkt.text, msg, sizeof(pkt.text) - 1);
    for (int i = 0; i < MAX_PLAYERS; i++) pkt.lives[i] = game->players[i].lives;
    pkt.current_player_id = (game->current_player_idx == -1)
                                ? 0
                                : game->players[game->current_player_idx].id;
    game_broadcast(game, &pkt);
}
