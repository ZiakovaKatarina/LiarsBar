#ifndef CLIENT_STATE_H
#define CLIENT_STATE_H

#include <pthread.h>
#include <stdbool.h>

#include "common.h"
#include "ipc_interface.h"

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define WHITE "\x1b[37m"
#define BOLD "\x1b[1m"

typedef struct ClientState {
    int fd;
    IPC_Interface ipc;

    int player_id;
    int game_id;

    int current_player_id;
    int current_bet_count;
    int current_bet_value;

    char last_message[256];
    bool message_is_error;

    bool is_running;
    bool game_over;
    bool round_started;

    int lives[MAX_PLAYERS];
    int my_cards[MAX_LIVES];

    pthread_mutex_t mutex;
} ClientState;

void client_state_init(ClientState* state, IPC_Interface ipc);
void client_state_destroy(ClientState* state);

bool client_connect(ClientState* state, const char* address, bool auto_start_server);
void client_disconnect(ClientState* state);

void client_send_join(ClientState* state, int game_id, int lives, int players);
void client_send_bet(ClientState* state, int count, int card_value);
void client_send_liar(ClientState* state);
void client_send_quit(ClientState* state);

bool client_process_packet(ClientState* state, GamePacket* pkt);

#endif