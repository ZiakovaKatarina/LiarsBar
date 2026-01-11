#ifndef SERVER_PLAYER_H
#define SERVER_PLAYER_H

#include <stdbool.h>

#include "common.h"
#include "ipc_interface.h"

typedef struct {
    int id;
    int fd;
    int lives;
    int cards[MAX_LIVES];
    bool is_active;

    IPC_Interface ipc;
} ServerPlayer;

void player_init(ServerPlayer* p, int id, int fd, int lives, IPC_Interface ipc);

void player_set_hand(ServerPlayer* p, const int* new_cards, int count);
void player_lose_life(ServerPlayer* p);
bool player_has_cards(ServerPlayer* p);

void player_send_packet(ServerPlayer* p, GamePacket* pkt);
void player_send_error(ServerPlayer* p, const char* msg);

int player_count_card(ServerPlayer* p, int card_value);

#endif
