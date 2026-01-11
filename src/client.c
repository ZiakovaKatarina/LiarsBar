#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../include/client_state.h"
#include "../include/client_ui.h"
#include "../include/common.h"
#include "../include/ipc_interface.h"

static int parse_input(const char* input, int* out_count, int* out_val) {
    if (strcasecmp(input, "liar") == 0) return 1;
    if (strcasecmp(input, "quit") == 0) return 2;

    char card_char;
    if (sscanf(input, "%d %c", out_count, &card_char) == 2) {
        switch (toupper(card_char)) {
            case 'Q':
                *out_val = CARD_QUEEN;
                return 3;  // Kód 3 = Bet
            case 'K':
                *out_val = CARD_KING;
                return 3;
            case 'A':
                *out_val = CARD_ACE;
                return 3;
            case 'J':
                *out_val = CARD_JOKER;
                return 3;
        }
    }
    return 0;
}

void* network_thread_func(void* arg) {
    ClientState* state = (ClientState*)arg;
    GamePacket pkt;

    while (state->is_running) {
        int res = state->ipc.receive_packet(state->fd, &pkt);

        if (res <= 0) {
            pthread_mutex_lock(&state->mutex);
            state->is_running = false;
            strcpy(state->last_message, "⚠️ Disconnected from server.");
            state->message_is_error = true;
            pthread_mutex_unlock(&state->mutex);
            ui_render_game(state);
            break;
        }

        client_process_packet(state, &pkt);

        ui_render_game(state);
    }
    return NULL;
}

void run_game_loop(ClientState* state) {
    pthread_t net_thread;

    if (pthread_create(&net_thread, NULL, network_thread_func, state) != 0) {
        printf("Error creating network thread.\n");
        return;
    }

    char buffer[256];
    int bet_count, bet_val;

    ui_render_game(state);

    while (state->is_running) {
        if (!fgets(buffer, sizeof(buffer), stdin)) break;

        buffer[strcspn(buffer, "\n")] = 0;
        if (strlen(buffer) == 0) continue;

        int cmd_type = parse_input(buffer, &bet_count, &bet_val);

        switch (cmd_type) {
            case 1:
                client_send_liar(state);
                break;
            case 2:
                client_send_quit(state);
                break;
            case 3:
                client_send_bet(state, bet_count, bet_val);
                break;
            default:
                pthread_mutex_lock(&state->mutex);
                snprintf(state->last_message, sizeof(state->last_message),
                         "❌ Invalid command. Try '2 K' or 'liar'.");
                state->message_is_error = true;
                pthread_mutex_unlock(&state->mutex);
                ui_render_game(state);
                break;
        }
    }

    state->is_running = false;
    pthread_join(net_thread, NULL);
}

int main() {
    IPC_Interface ipc = get_socket_interface();
    ClientState state;

    client_state_init(&state, ipc);

    while (1) {
        ui_show_welcome();
        printf("1. New Game\n");
        printf("2. Join Game\n");
        printf("3. Rules\n");
        printf("4. Exit\n\n");

        int choice = ui_get_int("Choice: ", 1, 4, 0);

        if (choice == 3) {
            ui_show_rules();
            continue;
        }
        if (choice == 4) break;

        if (!client_connect(&state, "127.0.0.1", true)) {
            printf(RED "❌ Could not connect to server.\n" RESET);
            ui_wait_enter();
            continue;
        }

        if (!fgets(input, sizeof(input), stdin)) {
            args->is_running = 0;
            break;
        }
        
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;

        if (strcmp(input, "quit") == 0) {
            printf(CYAN "\nLeaving game and returning to menu...\n" RESET);
            args->intentional_quit = true;
            GamePacket quit_pkt = {.MessageType = MSG_QUIT};
            ipc.send_packet(args->fd, &quit_pkt);
            args->is_running = 0;
            ipc.close_conn(args->fd);
            break;
        }

        GamePacket pkt = parse_bet_input(input);
        if (pkt.MessageType == -1) {
            printf(RED "Invalid command. Try: '3 K' or 'liar'\n" RESET);
            if (args->current_player_id == args->player_id) {
                printf(BLUE "> " RESET);
                fflush(stdout);
            }
            continue;
        }
        ipc.send_packet(args->fd, &pkt);
    }

    args->is_running = 0;
    pthread_join(recv_tid, NULL);

    if (args->wait_for_enter) {
        wait_for_enter();
        args->wait_for_enter = false;
    }

    if (args->fd >= 0) {
        ipc.close_conn(args->fd);
    }
    printf(CYAN "\nReturning to main menu...\n\n" RESET);
}

void show_rules(void) {
    printf(BOLD YELLOW "\n╔══════════════════════════════════════════════════╗\n");
    printf("║              LIAR'S BAR GAME RULES               ║\n");
    printf("╚══════════════════════════════════════════════════╝" RESET "\n");

    printf("- Game for " BOLD "2–4 players" RESET " with deck:\n");
    printf("      → 6× Q (queen),\n");
    printf("      → 6× K (king),\n");
    printf("      → 6× A (ace),\n");
    printf("      → 2× J (" BOLD "joker" RESET ").\n");
    printf("- The game creator selects the " BOLD "initial number of lives" RESET "\n");
    printf("  for all players before the game starts.\n");
    printf("- Each player starts with a number of cards equal to their\n");
    printf("  current " BOLD "lives" RESET ".\n");
    printf("- Players take turns betting on the " BOLD "total count and value of cards" RESET " on table.\n");
    printf("- Example: \"5 K\" = at least 5 kings (including jokers as wildcard).\n");
    printf("- Value order: " BOLD "Q < K < A < J" RESET "\n");
    printf("- " BOLD "Joker (J)" RESET " counts as all values.\n");
    printf("- New bet must be " BOLD "higher" RESET " than previous:\n");
    printf("      → higher count, or\n");
    printf("      → same count and higher value.\n");
    printf("- Bet count " BOLD "cannot be higher" RESET " than the total number of lives\n");
    printf("  of all players combined.\n");
    printf("- Player on turn can either bet or call " BOLD "liar" RESET ".\n");
    printf("- If liar " BOLD "succeeds" RESET " (fewer than bet) → bettor loses one life.\n");
    printf("- If liar " BOLD "fails" RESET " (at least as many) → caller loses one life.\n");
    printf("- After losing a life, " BOLD "new cards are dealt" RESET " based on current lives.\n");
    printf("- Game ends when only one player has lives → they win.\n");
    printf("- Command " BOLD "quit" RESET " anytime during game = return to menu.\n\n");
}

int get_int_input(const char* prompt, int min, int max, int default_val) {
    char formatted_prompt[256];
    snprintf(formatted_prompt, sizeof(formatted_prompt), prompt, default_val, min, max);
    printf(YELLOW "%s" RESET, formatted_prompt);
    fflush(stdout);
    
    char line[10];
    if (!fgets(line, sizeof(line), stdin)) return default_val;
    line[strcspn(line, "\n")] = 0;
    
    if (strlen(line) == 0) return default_val;
    
    int value = atoi(line);
    if (value >= min && value <= max) {
        return value;
    }
    
    printf(YELLOW "⚠️  Invalid value, using default (%d).\n" RESET, default_val);
    return default_val;
}

char* get_game_id_input(void) {
    printf(YELLOW "Enter game ID (or 'quit' to return): " RESET);
    fflush(stdout);
    
    static char id_line[20];
    if (!fgets(id_line, sizeof(id_line), stdin)) return NULL;
    id_line[strcspn(id_line, "\n")] = 0;
    
    return id_line;
}

void handle_new_game(IPC_Interface ipc) {
    ClientThreadArgs args;
    init_client_args(&args, ipc);
    
    int max_players = get_int_input(
        "\n👥 How many players? (default: %d, min: %d, max: %d): ",
        MIN_PLAYERS, MAX_PLAYERS, MAX_PLAYERS
    );
    printf(GREEN "✓ Game for %d players.\n" RESET, max_players);

    int initial_lives = get_int_input(
        "💚 How many lives at start? (default: %d, min: %d, max: %d): ",
        1, MAX_LIVES, 3
    );
    printf(GREEN "✓ Game will start with %d lives per player.\n\n" RESET, initial_lives);

    start_server_if_needed(&args, ipc);
    if (args.fd < 0) return;

    GamePacket join_pkt = {.MessageType = MSG_JOIN, .game_id = 0};
    join_pkt.count = initial_lives;
    join_pkt.player_id = max_players;
    strcpy(join_pkt.text, "I am host");
    ipc.send_packet(args.fd, &join_pkt);

    game_loop(&args, ipc);
}

void handle_join_game(IPC_Interface ipc) {
    printf(GREEN "Joining game...\n" RESET);
    
    char* id_line = get_game_id_input();
    if (!id_line) return;
    
    if (strcmp(id_line, "quit") == 0 || strcmp(id_line, "q") == 0) {
        printf(CYAN "Returning to main menu...\n\n" RESET);
        return;
    }
    
    int game_id = atoi(id_line);
    if (game_id <= 0) {
        printf(RED "❌ Invalid ID (must be > 0 or 'quit').\n\n" RESET);
        return;
    }

    ClientThreadArgs args;
    init_client_args(&args, ipc);

    args.fd = ipc.init_client("127.0.0.1");
    if (args.fd < 0) {
        printf(RED "❌ Failed to connect to server.\n\n" RESET);
        return;
    }
    
    GamePacket join_pkt = {.MessageType = MSG_JOIN, .game_id = game_id};
    join_pkt.count = 0;
    strcpy(join_pkt.text, "I joined");
    ipc.send_packet(args.fd, &join_pkt);
    
    game_loop(&args, ipc);
}

int main() {
    IPC_Interface socket_ipc = get_socket_interface();

    while(1) {
        printf(BOLD CYAN "╔════════════════════════════════╗\n");
        printf("║        🎰 LIAR'S BAR 🎰        ║\n");
        printf("╚════════════════════════════════╝" RESET "\n");

        printf(BOLD "Menu:" RESET "\n");
        printf(YELLOW "1." RESET " New game\n");
        printf(YELLOW "2." RESET " Join game\n");
        printf(YELLOW "3." RESET " Exit\n");
        printf(YELLOW "4." RESET " Game rules\n\n");
        printf(BLUE "Choice: " RESET);
        fflush(stdout);

        char line[10];
        if (!fgets(line, sizeof(line), stdin)) break;

        switch(line[0]) {
            case '1':
                handle_new_game(socket_ipc);
                break;
            case '2':
                handle_join_game(socket_ipc);
                break;
            case '3':
                printf(CYAN "Goodbye!\n" RESET);
                return 0;
            case '4':
                show_rules();
                break;
            default:
                printf(RED "Invalid choice.\n" RESET);
                break;
        }
    }

    return 0;
}
