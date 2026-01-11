#ifndef SERVER_GAME_H
#define SERVER_GAME_H

#include <pthread.h>

#include "server_player.h"

typedef struct {
    int game_id;
    bool is_running;
    int initial_lives;
    ServerPlayer players[MAX_PLAYERS];
    int max_players;
    int connected_count;
    int current_player_idx;
    int round_active;
    int current_bet_count;
    int current_bet_value;
    int last_bettor_idx;
    pthread_mutex_t mutex;
    IPC_Interface ipc;
} ServerGame;

void game_init(ServerGame* game, int id, int max_players, int initial_lives,
               IPC_Interface ipc);
void game_destroy(ServerGame* game);

int game_add_player(ServerGame* game, int fd, int lives);
void game_remove_player(ServerGame* game, int player_idx);

void game_start_round(ServerGame* game);
void game_process_bet(ServerGame* game, int player_idx, int count, int value);
void game_process_liar(ServerGame* game, int caller_idx);

void game_broadcast(ServerGame* game, GamePacket* pkt);
void game_broadcast_update(ServerGame* game, const char* msg);

#endif
