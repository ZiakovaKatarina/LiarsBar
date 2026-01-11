#include "../include/client_ui.h"
#include "../include/common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Jednoduchý výpis karty
static void print_card(int value) {
    const char* symbols[] = {"Q", "K", "A", "J"};
    if (value >= 0 && value <= 3) {
        if (value == CARD_JOKER) printf(MAGENTA "%s" RESET, symbols[value]);
        else printf(WHITE "%s" RESET, symbols[value]);
    } else {
        printf("?");
    }
}

void ui_show_welcome() {
    printf(BOLD CYAN "╔════════════════════════════════╗\n");
    printf("║        🎰 LIAR'S BAR 🎰        ║\n");
    printf("╚════════════════════════════════╝" RESET "\n");
}

void ui_show_rules(void) {
    printf(BOLD YELLOW "\n╔══════════════════════════════════════════════════╗\n");
    printf("║              LIAR'S BAR GAME RULES               ║\n");
    printf("╚══════════════════════════════════════════════════╝" RESET "\n");

    printf("- Game for " BOLD YELLOW "2–4 players" RESET " with deck:\n");
    printf("      → 6× Q (queen),\n");
    printf("      → 6× K (king),\n");
    printf("      → 6× A (ace),\n");
    printf("      → 2× J (" BOLD YELLOW "joker" RESET ").\n");
    printf("- The game creator selects the " BOLD YELLOW"initial number of lives" RESET "\n");
    printf("  for all players before the game starts.\n");
    printf("- Each player starts with a number of cards equal to their\n");
    printf("  current " BOLD YELLOW "lives" RESET ".\n");
    printf("- Players take turns betting on the " BOLD YELLOW "total count and value of cards" RESET " on table.\n");
    printf("- Example: \"5 K\" = at least 5 kings (including jokers as wildcard).\n");
    printf("- Value order: " BOLD YELLOW "Q < K < A < J" RESET "\n");
    printf("- " BOLD YELLOW "Joker (J)" RESET " counts as all values.\n");
    printf("- New bet must be " BOLD YELLOW "higher" RESET " than previous:\n");
    printf("      → higher count, or\n");
    printf("      → same count and higher value.\n");
    printf("- Bet count " BOLD YELLOW "cannot be higher" RESET " than the total number of lives\n");
    printf("  of all players combined.\n");
    printf("- Player on turn can either bet or call " BOLD "liar" RESET ".\n");
    printf("- If liar " BOLD YELLOW "succeeds" RESET " (fewer than bet) → bettor loses one life.\n");
    printf("- If liar " BOLD YELLOW "fails" RESET " (at least as many) → caller loses one life.\n");
    printf("- After losing a life, " BOLD YELLOW "new cards are dealt" RESET " based on current lives.\n");
    printf("- Game ends when only one player has lives → they win.\n");
    printf("- Command " BOLD YELLOW "quit" RESET " anytime during game = return to menu.\n\n");
}

int ui_get_int(const char* prompt, int min, int max, int default_value) {
    char line[64];
    char formatted_prompt[128];

    snprintf(formatted_prompt, sizeof(formatted_prompt), prompt, default_value, min, max); // pôvodne psnprintf
    printf("%s", formatted_prompt);
    fflush(stdout);
    
    if (!fgets(line, sizeof(line), stdin)) {
        printf(YELLOW "⚠️  Invalid value, using default (%d).\n" RESET, default_value);
        return default_value;
    }

    line[strcspn(line, "\n")] = 0;
    if (strlen(line) == 0) {
        printf(YELLOW "⚠️  Invalid value, using default (%d).\n" RESET, default_value);
        return default_value;
    }

    int value = atoi(line);
    if (value < min || value > max) {
        printf(YELLOW "⚠️  Invalid value, using default (%d).\n" RESET, default_value);
        return default_value;
    }
    
    return value;
}

void ui_render_game(ClientState* state) {
    pthread_mutex_lock(&state->mutex);

    printf(BOLD BLUE "\n╔════════════════════════════════╗\n");
    printf("║          📊 GAME STATE         ║\n");
    printf("╚════════════════════════════════╝" RESET "\n");
    printf(BOLD BLUE "Game #%d | You are Player %d" RESET "\n", state->game_id, state->player_id);

    printf(MAGENTA "❤️  Lives:" RESET "\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->lives[i] > 0 || i + 1 == state->player_id) {
            printf("   Player %d: ", i + 1);
            for (int j = 0; j < state->lives[i]; j++) printf(RED "❤️ " RESET);
            printf(MAGENTA " (%d)" RESET, state->lives[i]);

            if (i + 1 == state->current_player_id)
                printf(BOLD CYAN " ◄ TURN" RESET);
            if (i + 1 == state->player_id) printf(YELLOW " (YOU)" RESET);
            printf("\n");
        }
    }
    printf("\n");

    if (state->current_bet_count > 0) {
        printf(YELLOW "💰 Current bet: %d x ", state->current_bet_count);
        print_card(state->current_bet_value);
        printf(RESET "\n");
    }

    printf(GREEN "📋 Your cards: " RESET);
    for (int i = 0; i < MAX_LIVES; i++) {
        if (state->my_cards[i] >= 0) {
            printf(GREEN);
            print_card(state->my_cards[i]);
            printf(" " RESET);
        }
    }
    printf("\n");

    if (strlen(state->last_message) > 0) {
        printf("%s %s" RESET "\n", state->message_is_error ? RED : GREEN, state->last_message);
    }

    if (state->game_over) {
        printf(BOLD RED "GAME OVER. Press ENTER to quit." RESET "\n");
    } else if (state->player_id == state->current_player_id) {
        printf(BOLD CYAN "👉 Your turn! Enter bet (e.g. '2 K') or 'liar': " RESET);
    } else {
        printf(WHITE "⏳ Waiting for Player %d..." RESET, state->current_player_id);
    }

    pthread_mutex_unlock(&state->mutex);
}

void ui_wait_enter() {
    printf(CYAN "\nPress Enter to return to main menu... " RESET);
    while (getchar() != '\n');
}