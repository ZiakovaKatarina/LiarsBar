// src/logic.c
#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/logic.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

void rozdaj_karty_vsetkym(int player_cards[MAX_PLAYERS][INITIAL_LIVES],
                          int sockets[MAX_PLAYERS],
                          IPC_Interface ipc,
                          int lives[MAX_PLAYERS],
                          int current_player) {
    int balicek[20] = {0};
    int k = 0;
    for (int i = 0; i < 6; i++) {
        balicek[k++] = 0; // Q
        balicek[k++] = 1; // K
        balicek[k++] = 2; // A
    }
    balicek[18] = 3; // J
    balicek[19] = 3; // J

    srand(time(NULL));
    for (int i = 19; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = balicek[i];
        balicek[i] = balicek[j];
        balicek[j] = temp;
    }

    // Rozdaj po 5 kariet každému hráčovi
    for (int hrac = 0; hrac < MAX_PLAYERS; hrac++) {
        if (sockets[hrac] != -1) {
            for (int c = 0; c < INITIAL_LIVES; c++) {
                player_cards[hrac][c] = balicek[hrac * INITIAL_LIVES + c];
            }

            GamePacket pkt = {0};
            pkt.MessageType = MSG_START_ROUND;
            strcpy(pkt.text, "Dostal si nové karty!");
            memcpy(pkt.my_cards, player_cards[hrac], sizeof(pkt.my_cards));
            memcpy(pkt.lives, lives, sizeof(pkt.lives));
            pkt.current_player_id = current_player + 1;  // ID od 1

            ipc.send_packet(sockets[hrac], &pkt);
        }
    }
}