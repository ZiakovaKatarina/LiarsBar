#include "../include/server_player.h"

#include <stdio.h>
#include <string.h>

void player_init(ServerPlayer* p, int id, int fd, int lives,
                 IPC_Interface ipc) {
    p->id = id;
    p->fd = fd;
    p->lives = lives;
    p->is_active = true;
    p->ipc = ipc;

    for (int i = 0; i < MAX_LIVES; i++) {
        p->cards[i] = -1;
    }
}

void player_set_hand(ServerPlayer* p, const int* new_cards, int count) {
    for (int i = 0; i < MAX_LIVES; i++) p->cards[i] = -1;

    for (int i = 0; i < count && i < p->lives; i++) {
        p->cards[i] = new_cards[i];
    }
}

void player_lose_life(ServerPlayer* p) {
    if (p->lives > 0) {
        p->lives--;
    }
    if (p->lives <= 0) {
        p->is_active = false;
    }
}

bool player_has_cards(ServerPlayer* p) { return p->lives > 0; }

void player_send_packet(ServerPlayer* p, GamePacket* pkt) {
    if (p->fd >= 0) {
        p->ipc.send_packet(p->fd, pkt);
    }
}

void player_send_error(ServerPlayer* p, const char* msg) {
    GamePacket pkt = {0};
    pkt.type = MSG_UPDATE;
    strncpy(pkt.text, msg, sizeof(pkt.text) - 1);
    if (strstr(msg, "❌") == NULL) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "❌ %s", msg);
        strncpy(pkt.text, buffer, sizeof(pkt.text) - 1);
    }
    player_send_packet(p, &pkt);
}

int player_count_card(ServerPlayer* p, int card_value) {
    int count = 0;
    for (int i = 0; i < p->lives; i++) {
        int c = p->cards[i];
        if (c != -1 && (c == card_value || c == CARD_JOKER)) {
            count++;
        }
    }
    return count;
}
