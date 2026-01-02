#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/logic.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void rozdaj_karty_vsetkym(int player_cards[MAX_PLAYERS][MAX_LIVES],
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
            for (int i = 0; i < MAX_LIVES; i++) {
                pkt.my_cards[i] = -1;
            }

            for (int i = 0; i < lives[hrac]; i++) {
                pkt.my_cards[i] = player_cards[hrac][i];
            }

            memcpy(pkt.lives, lives, sizeof(pkt.lives));
            pkt.current_player_id = current_player + 1;

            ipc.send_packet(sockets[hrac], &pkt);
        }
    }
}

bool je_vhodna_stávka(int current_count, int current_value,
                      int new_count, int new_value) {
    if (current_count == 0 && current_value == -1) {
        return true;
    }
    
    if (new_count > current_count) {
        return true;
    }
    
    if (new_count == current_count && new_value > current_value) {
        return true;
    }
    
    return false;
}

int spocitaj_karty(int player_cards[MAX_PLAYERS][MAX_LIVES],
                   int lives[MAX_PLAYERS],
                   int target_value) {
    int total_count = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (lives[p] > 0) {
            for (int c = 0; c < lives[p]; c++) {
                if (player_cards[p][c] == target_value || 
                    player_cards[p][c] == CARD_JOKER) {
                    total_count++;
                }
            }
        }
    }
    return total_count;
}

int spocitaj_celkove_karty(int lives[MAX_PLAYERS]) {
    int total_cards = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (lives[i] > 0) {
            total_cards += lives[i];
        }
    }
    return total_cards;
}

int zisti_pocet_zivych(int lives[MAX_PLAYERS], int *winner_id) {
    int alive_count = 0;
    *winner_id = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (lives[i] > 0) {
            alive_count++;
            *winner_id = i;
        }
    }
    return alive_count;
}

int dalsi_hrac(int current_player, int lives[MAX_PLAYERS]) {
    int next_player = current_player;
    int tries = 0;
    do {
        next_player = (next_player + 1) % MAX_PLAYERS;
        tries++;
    } while (tries < MAX_PLAYERS && lives[next_player] <= 0);
    
    if (tries >= MAX_PLAYERS) {
        return -1;
    }
    return next_player;
}

void evaluate_liar(int player_cards[MAX_PLAYERS][MAX_LIVES],
                   int lives[MAX_PLAYERS],
                   int called_value,
                   int bet_count,
                   int last_bettor,
                   int caller_id,
                   int *loser_id,
                   bool *liar_succeeds) {
    
    int total_count = spocitaj_karty(player_cards, lives, called_value);
    *liar_succeeds = (total_count < bet_count);
    
    if (*liar_succeeds) {
        *loser_id = last_bettor;
    } else {
        *loser_id = caller_id;
    }
    
    if (*loser_id > 0 && *loser_id <= MAX_PLAYERS) {
        lives[*loser_id - 1]--;
    }
}