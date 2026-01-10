#include "../include/common.h"
#include "../include/ipc_interface.h"
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <poll.h>

#define SERVER_PATH "./server"

typedef struct ClientThreadArgs {
    int fd;
    IPC_Interface ipc;
    volatile int is_running;
    volatile bool intentional_quit;
    int player_id;
    int game_id;
    int lives[MAX_PLAYERS];
    int current_player_id;
    int last_current_player_id;
    int current_bet_count;
    int current_bet_value;
    bool game_started;
    int my_cards[MAX_LIVES];
    volatile bool wait_for_enter;
} ClientThreadArgs;

void print_card(int value) {
    const char* symbols[] = {"Q", "K", "A", "J"};
    if (value >= 0 && value <= 3) {
        printf("%s", symbols[value]);
    } else {
        printf("?");
    }
}

void wait_for_enter(void) {
    printf(CYAN "\nPress Enter to return to main menu... " RESET);
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void print_game_state(ClientThreadArgs *args) {
    if (args->current_player_id == 0) return;

    printf(BOLD BLUE "\n╔════════════════════════════════╗\n");
    printf("║          📊 GAME STATE         ║\n");
    printf("╚════════════════════════════════╝" RESET "\n");
    
    printf(MAGENTA "❤️  Lives:" RESET "\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (args->lives[i] > 0 || i + 1 == args->player_id) {
            printf("   Player %d: ", i + 1);
            for (int j = 0; j < args->lives[i]; j++) printf(RED "❤️ " RESET);
            printf(MAGENTA " (%d)" RESET "\n", args->lives[i]);
        }
    }
    printf("\n");

    if (args->current_bet_count > 0) {
        printf(YELLOW "💰 Current bet: %d x ", args->current_bet_count);
        print_card(args->current_bet_value);
        printf(RESET "\n");
    }

    printf(CYAN "➡️  Turn:  Player %d" RESET, args->current_player_id);
    if (args->current_player_id == args->player_id) {
        printf(RED " " BOLD "◄── YOU!" RESET "\n");
    } else {
        printf("\n");
    }

    printf(GREEN "📋 Your cards: " RESET);
    for (int i = 0; i < MAX_LIVES; i++) {
        if (args->my_cards[i] >= 0) {
            printf(GREEN);
            print_card(args->my_cards[i]);
            printf(" " RESET);
        }
    }
    printf("\n");
}

void init_client_args(ClientThreadArgs *args, IPC_Interface ipc) {
    memset(args, 0, sizeof(ClientThreadArgs));
    args->ipc = ipc;
    args->is_running = 1;
    args->fd = -1;
}

GamePacket parse_bet_input(const char* input) {
    GamePacket pkt = {0};
    
    if (strcasecmp(input, "liar") == 0) {
        pkt.MessageType = MSG_LIAR;
        return pkt;
    }
    
    int count;
    char card_char;
    if (sscanf(input, "%d %c", &count, &card_char) != 2) {
        pkt.MessageType = -1;
        return pkt;
    }
    
    int value = -1;
    switch(toupper(card_char)) {
        case 'Q': value = CARD_QUEEN; break;
        case 'K': value = CARD_KING; break;
        case 'A': value = CARD_ACE; break;
        case 'J': value = CARD_JOKER; break;
    }
    
    if (value != -1 && count > 0) {
        pkt.MessageType = MSG_BET;
        pkt.count = count;
        pkt.card_value = value;
    } else {
        pkt.MessageType = -1;
    }
    return pkt;
}

void start_server_if_needed(ClientThreadArgs *args, IPC_Interface ipc) {
    args->fd = ipc.init_client("127.0.0.1");
    
    if (args->fd < 0) {
        printf(YELLOW "⚠️  Server not running - starting automatically...\n" RESET);
        
        if (system(SERVER_PATH " &") == -1) {
            printf(RED "❌ Failed to start server!\n" RESET);
            printf(YELLOW "Try starting server manually:\n   ./server\n\n" RESET);
            return;
        }
        
        for (int i = 0; i < 10 && args->fd < 0; i++) {
            usleep(200000);
            args->fd = ipc.init_client("127.0.0.1");
        }
        
        if (args->fd < 0) {
            printf(RED "❌ Still cannot connect to server.\n" RESET);
            printf(YELLOW "Try again or start server manually.\n\n" RESET);
            return;
        }
    }

    printf(CYAN "✅ Connected to server.\n" RESET);
}

void handle_message_welcome(ClientThreadArgs *args, GamePacket *pkt) {
    args->player_id = pkt->player_id;
    args->game_id = pkt->game_id;
    memcpy(args->lives, pkt->lives, sizeof(pkt->lives));
    printf(CYAN "[SERVER]: 🎮 Welcome! You are Player %d in game %d." RESET "\n", 
           pkt->player_id, pkt->game_id);
}

void handle_message_start_round(ClientThreadArgs *args, GamePacket *pkt) {
    args->game_started = true;
    printf(YELLOW "\n╔════════════════════════════════╗\n");
    printf("║        🎯 NEW ROUND 🎯         ║\n");
    printf("╚════════════════════════════════╝" RESET "\n");
    printf(GREEN "📋 Your cards: " RESET);
    
    for(int i = 0; i < MAX_LIVES; i++) {
        if (pkt->my_cards[i] >= 0) {
            printf(GREEN);
            print_card(pkt->my_cards[i]);
            printf(" " RESET);
        }
    }
    printf("\n");

    memcpy(args->lives, pkt->lives, sizeof(pkt->lives));
    memcpy(args->my_cards, pkt->my_cards, sizeof(pkt->my_cards));
    args->current_player_id = pkt->current_player_id;
    args->last_current_player_id = 0;
    args->current_bet_count = 0;
    args->current_bet_value = -1;
}

void handle_message_update(ClientThreadArgs *args, GamePacket *pkt) {
    memcpy(args->lives, pkt->lives, sizeof(pkt->lives));
    
    if (pkt->current_player_id != 0) {
        args->current_player_id = pkt->current_player_id;
    }
    if (pkt->count > 0) {
        args->current_bet_count = pkt->count;
        args->current_bet_value = pkt->card_value;
    }

    bool is_fatal_error = (strstr(pkt->text, "not exist") != NULL) ||
                          (strstr(pkt->text, "full") != NULL) ||
                          (strstr(pkt->text, "Server is full") != NULL);

    if (strstr(pkt->text, "❌") != NULL && is_fatal_error) {
        printf(RED "%s\n" RESET, pkt->text);
        args->wait_for_enter = true;
        args->is_running = 0;
        return;
    }

    bool waiting = (args->current_player_id == 0 && 
                   args->current_bet_count == 0 && 
                   !args->game_started);
    if (waiting) {
        printf("%s\n", pkt->text);
        return;
    }

    printf(BLUE "[UPDATE]: %s" RESET "\n", pkt->text);

    if (args->current_player_id != args->last_current_player_id && 
        args->current_player_id != 0) {
        print_game_state(args);
        args->last_current_player_id = args->current_player_id;
        if (args->current_player_id == args->player_id) {
            printf(BLUE "> " RESET);
            fflush(stdout);
        }
    } else if (strstr(pkt->text, "❌ Bet") != NULL && 
               args->current_player_id == args->player_id) {
        printf(BLUE "> " RESET);
        fflush(stdout);
    }
}

void handle_message_game_over(ClientThreadArgs *args, GamePacket *pkt) {
    printf(BOLD YELLOW "\n╔════════════════════════════════╗\n");
    printf("║        🏆 GAME OVER 🏆         ║\n");
    printf("╚════════════════════════════════╝" RESET "\n");
    printf(GREEN "%s" RESET "\n", pkt->text);
    args->wait_for_enter = true;
    args->is_running = 0;
}

void* receive_thread(void* arg) {
    ClientThreadArgs *args = (ClientThreadArgs*)arg;
    GamePacket pkt;

    while(args->is_running) {
        int res = args->ipc.receive_packet(args->fd, &pkt);

        if (res <= 0) {
            if (!args->intentional_quit) {
                printf(RED BOLD "\n╔════════════════════════════════════╗\n");
                printf("║      ⚠️   SERVER TERMINATED ⚠️       ║\n");
                printf("╚════════════════════════════════════╝" RESET "\n");
                printf(YELLOW "Connection to server lost.\n");
                printf("Server was probably shut down or crashed.\n" RESET);
                fflush(stdout);
                args->wait_for_enter = true;
            }
            args->is_running = 0;
            break;
        }

        switch(pkt.MessageType) {
            case MSG_WELCOME:
                handle_message_welcome(args, &pkt);
                break;
            case MSG_START_ROUND:
                handle_message_start_round(args, &pkt);
                break;
            case MSG_UPDATE:
                handle_message_update(args, &pkt);
                break;
            case MSG_GAME_OVER:
                handle_message_game_over(args, &pkt);
                break;
            default:
                break;
        }
    }
    return NULL;
}

void game_loop(ClientThreadArgs *args, IPC_Interface ipc) {
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receive_thread, args);

    char input[256];
    struct pollfd pfd = { .fd = 0, .events = POLLIN };

    while (args->is_running) {
        if (!args->game_started) {
            usleep(100000);
            continue;
        }

        int ret = poll(&pfd, 1, 100);
        if (ret <= 0) {
            if (ret < 0) break;
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