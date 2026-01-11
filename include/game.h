#ifndef GAME_H
#define GAME_H

#include "common.h"
#include <stdbool.h>

typedef struct Game Game;

Game* game_create(int max_players);
void  game_destroy(Game* g);

// Updates
void  game_set_lives(Game* g, const int lives[MAX_PLAYERS]);
void  game_set_current_player(Game* g, int player_id);

// Round management
void  game_start_round(Game* g);

// Queries
int   game_get_current_player(const Game* g);
void  game_get_lives(const Game* g, int out[MAX_PLAYERS]);
void  game_get_player_cards(const Game* g, int player_id, int out[MAX_LIVES]);

// Bet management
bool game_place_bet(Game* g, int player_id, int count, int value);
bool game_call_liar(Game* g, int caller_id, int* loser_id, bool* liar_succeeds);

// State queries
int game_count_total_cards(const Game* g);
int game_count_alive_players(const Game* g, int* winner_id);
int game_next_player(const Game* g, int from_player);

#endif