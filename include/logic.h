#ifndef LOGIC_H
#define LOGIC_H

#include "common.h"
#include "ipc_interface.h"
#include <stdbool.h>

void rozdaj_karty_vsetkym(int player_cards[MAX_PLAYERS][MAX_LIVES],
                          int sockets[MAX_PLAYERS],
                          IPC_Interface ipc,
                          int lives[MAX_PLAYERS],
                          int current_player);

bool je_vhodna_stávka(int current_count, int current_value,
                      int new_count, int new_value);

int spocitaj_karty(int player_cards[MAX_PLAYERS][MAX_LIVES],
                   int lives[MAX_PLAYERS],
                   int target_value);

int spocitaj_celkove_karty(int lives[MAX_PLAYERS]);

int zisti_pocet_zivych(int lives[MAX_PLAYERS], int *winner_id);

int dalsi_hrac(int current_player, int lives[MAX_PLAYERS]);

void evaluate_liar(int player_cards[MAX_PLAYERS][MAX_LIVES],
                   int lives[MAX_PLAYERS],
                   int called_value,
                   int bet_count,
                   int last_bettor,
                   int caller_id,
                   int *loser_id,
                   bool *liar_succeeds);

#endif