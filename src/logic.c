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
        balicek[k++] = CARD_QUEEN;
        balicek[k++] = CARD_KING;
        balicek[k++] = CARD_ACE;
    }
    balicek[18] = CARD_JOKER;
    balicek[19] = CARD_JOKER;

    srand(time(NULL));
    for (int i = 19; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = balicek[i];
        balicek[i] = balicek[j];
        balicek[j] = temp;
    }

    int card_index = 0;
    for (int hrac = 0; hrac < MAX_PLAYERS; hrac++) {
        if (sockets[hrac] != -1 && lives[hrac] > 0) {
            for (int c = 0; c < lives[hrac]; c++) {
                player_cards[hrac][c] = balicek[card_index++];
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