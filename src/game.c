#include "../include/game.h"
#include "../include/logic.h"
#include <string.h>
#include <stdlib.h>

struct Game {
    int max_players;
    int lives[MAX_PLAYERS];
    int player_cards[MAX_PLAYERS][MAX_LIVES];
    int current_player;
};

Game* game_create(int max_players) {
    Game* g = (Game*)calloc(1, sizeof(Game));
    if (!g) return NULL;
    g->max_players = max_players;
    g->current_player = -1;
    memset(g->lives, 0, sizeof(g->lives));
    memset(g->player_cards, -1, sizeof(g->player_cards));
    return g;
}

void game_destroy(Game* g) { free(g); }

void game_set_lives(Game* g, const int lives[MAX_PLAYERS]) {
    memcpy(g->lives, lives, sizeof(g->lives));
}

void game_set_current_player(Game* g, int player_id) {
    g->current_player = player_id;
}

int game_get_current_player(const Game* g) { return g->current_player; }

void game_get_lives(const Game* g, int out[MAX_PLAYERS]) {
    memcpy(out, g->lives, sizeof(g->lives));
}

void game_get_player_cards(const Game* g, int player_id, int out[MAX_LIVES]) {
    if (player_id < 0 || player_id >= g->max_players) {
        memset(out, -1, sizeof(int) * MAX_LIVES);
        return;
    }
    memcpy(out, g->player_cards[player_id], sizeof(int) * MAX_LIVES);
}

void game_start_round(Game* g) {
    int start_from = g->current_player < 0 ? 0 : g->current_player;
    g->current_player = next_player(start_from, g->lives);
    deal_cards_to_all(g->player_cards, g->lives);
}