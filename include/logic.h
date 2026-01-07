#ifndef LOGIC_H
#define LOGIC_H

#include "common.h"
#include "ipc_interface.h"
#include <stdbool.h>

void deal_cards_to_all(int player_cards[MAX_PLAYERS][MAX_LIVES],
                       int sockets[MAX_PLAYERS],
                       IPC_Interface ipc,
                       int lives[MAX_PLAYERS],
                       int current_player);

bool is_valid_bet(int current_count, int current_value,
                  int new_count, int new_value);

int count_cards(int player_cards[MAX_PLAYERS][MAX_LIVES],
                int lives[MAX_PLAYERS],
                int target_value);

int count_total_cards(int lives[MAX_PLAYERS]);

int count_alive_players(int lives[MAX_PLAYERS], int *winner_id);

int next_player(int current_player, int lives[MAX_PLAYERS]);

void evaluate_liar(int player_cards[MAX_PLAYERS][MAX_LIVES],
                   int lives[MAX_PLAYERS],
                   int called_value,
                   int bet_count,
                   int last_bettor,
                   int caller_id,
                   int *loser_id,
                   bool *liar_succeeds);

#endif