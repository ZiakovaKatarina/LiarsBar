#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/logic.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void deal_cards_to_all(int player_cards[MAX_PLAYERS][MAX_LIVES],
                       int sockets[MAX_PLAYERS],
                       IPC_Interface ipc,
                       int lives[MAX_PLAYERS],
                       int current_player) {
    int deck[20] = {0};
    int k = 0;
    for (int i = 0; i < 6; i++) {
        deck[k++] = CARD_QUEEN;
        deck[k++] = CARD_KING;
        deck[k++] = CARD_ACE;
    }
    deck[18] = CARD_JOKER;
    deck[19] = CARD_JOKER;

    srand(time(NULL));
    for (int i = 19; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }

    int card_index = 0;
    for (int player = 0; player < MAX_PLAYERS; player++) {
        if (sockets[player] != -1 && lives[player] > 0) {
            for (int c = 0; c < lives[player]; c++) {
                player_cards[player][c] = deck[card_index++];
            }

            GamePacket pkt = {0};
            pkt.MessageType = MSG_START_ROUND;
            strcpy(pkt.text, "You received new cards!");
            for (int i = 0; i < MAX_LIVES; i++) {
                pkt.my_cards[i] = -1;
            }
            for (int i = 0; i < lives[player]; i++) {
                pkt.my_cards[i] = player_cards[player][i];
            }
            memcpy(pkt.lives, lives, sizeof(pkt.lives));
            pkt.current_player_id = current_player + 1;
            ipc.send_packet(sockets[player], &pkt);
        }
    }
}

bool is_valid_bet(int current_count, int current_value,
                  int new_count, int new_value) {
    if (current_count == 0 && current_value == -1) return true;
    if (new_count > current_count) return true;
    if (new_count == current_count && new_value > current_value) return true;
    return false;
}

int count_cards(int player_cards[MAX_PLAYERS][MAX_LIVES],
                int lives[MAX_PLAYERS],
                int target_value) {
    int total_count = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (lives[p] > 0) {
            for (int c = 0; c < lives[p]; c++) {
                if (player_cards[p][c] >= 0 && 
                    (player_cards[p][c] == target_value || 
                     player_cards[p][c] == 3)) {
                    total_count++;
                }
            }
        }
    }
    return total_count;
}

int count_total_cards(int lives[MAX_PLAYERS]) {
    int total = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (lives[i] > 0) total += lives[i];
    }
    return total;
}

int count_alive_players(int lives[MAX_PLAYERS], int *winner_id) {
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

int next_player(int current_player, int lives[MAX_PLAYERS]) {
    int next = current_player;
    int tries = 0;
    do {
        next = (next + 1) % MAX_PLAYERS;
        tries++;
    } while (tries < MAX_PLAYERS && lives[next] <= 0);
    
    return (tries >= MAX_PLAYERS) ? -1 : next;
}

void evaluate_liar(int player_cards[MAX_PLAYERS][MAX_LIVES],
                   int lives[MAX_PLAYERS],
                   int called_value,
                   int bet_count,
                   int last_bettor,
                   int caller_id,
                   int *loser_id,
                   bool *liar_succeeds) {
    
    int total_count = count_cards(player_cards, lives, called_value);
    *liar_succeeds = (total_count < bet_count);
    *loser_id = *liar_succeeds ? last_bettor : caller_id;
}