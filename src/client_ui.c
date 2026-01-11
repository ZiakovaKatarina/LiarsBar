#include "../include/client_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_card_symbol(int value) {
    const char* symbols[] = {"Q", "K", "A", "J"};
    if (value >= 0 && value <= 3) {
        if (value == CARD_JOKER)
            printf(MAGENTA "%s" RESET, symbols[value]);
        else
            printf(WHITE "%s" RESET, symbols[value]);
    } else {
        printf("?");
    }
}

void ui_show_welcome() {
    printf(CLEAR_SCREEN);
    printf(BOLD CYAN "╔════════════════════════════════╗\n");
    printf("║        🎰 LIAR'S BAR 🎰        ║\n");
    printf("╚════════════════════════════════╝" RESET "\n\n");
}

void ui_show_rules() {
    printf(CLEAR_SCREEN);
    printf(BOLD YELLOW "📜 GAME RULES:\n" RESET);
    printf("1. Q < K < A < J (Joker is wildcard)\n");
    printf("2. Bet higher count OR same count + higher value.\n");
    printf("3. 'Liar' calls out the previous bet.\n");
    printf("4. Loser loses a life (card).\n\n");
    ui_wait_enter();
}

void ui_render_game(ClientState* state) {
    pthread_mutex_lock(&state->mutex);

    printf(CLEAR_SCREEN);

    printf(BOLD BLUE "Game #%d | You are Player %d" RESET "\n", state->game_id,
           state->player_id);
    printf("──────────────────────────────────\n");

    if (strlen(state->last_message) > 0) {
        printf("%s %s" RESET "\n", state->message_is_error ? RED : GREEN,
               state->last_message);
        printf("──────────────────────────────────\n");
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->lives[i] > 0 ||
            (state->round_started && i + 1 == state->player_id)) {
            printf("Player %d: ", i + 1);
            for (int L = 0; L < state->lives[i]; L++) printf(RED "❤️ " RESET);

            if (i + 1 == state->current_player_id)
                printf(BOLD CYAN " ◄ TURN" RESET);
            if (i + 1 == state->player_id) printf(YELLOW " (YOU)" RESET);
            printf("\n");
        }
    }
    printf("\n");

    printf(BOLD "💰 Table Bet: " RESET);
    if (state->current_bet_count > 0) {
        printf(YELLOW "%d x ", state->current_bet_count);
        print_card_symbol(state->current_bet_value);
        printf(RESET "\n");
    } else {
        printf(WHITE "(Empty table)" RESET "\n");
    }
    printf("\n");

    printf(GREEN "🃏 Your Hand: " RESET);
    for (int i = 0; i < MAX_LIVES; i++) {
        if (state->my_cards[i] != -1) {
            printf("[");
            print_card_symbol(state->my_cards[i]);
            printf("] ");
        }
    }
    printf("\n\n");

    if (state->game_over) {
        printf(BOLD RED "GAME OVER. Press ENTER to quit." RESET "\n");
    } else if (state->player_id == state->current_player_id) {
        printf(BOLD CYAN
               "👉 Your turn! Enter bet (e.g. '2 K') or 'liar': " RESET);
    } else {
        printf(WHITE "⏳ Waiting for Player %d..." RESET,
               state->current_player_id);
    }

    fflush(stdout);

    pthread_mutex_unlock(&state->mutex);
}

int ui_get_int(const char* prompt, int min, int max, int def_val) {
    char buffer[64];
    printf("%s", prompt);
    if (!fgets(buffer, sizeof(buffer), stdin)) return def_val;

    int val = atoi(buffer);
    if (val < min || val > max) return def_val;
    return val;
}

void ui_wait_enter() {
    printf("Press ENTER to continue...");
    while (getchar() != '\n');
}
