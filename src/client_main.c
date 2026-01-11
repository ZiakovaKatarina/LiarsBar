#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>

#include "../include/client_state.h"
#include "../include/client_ui.h"
#include "../include/common.h"
#include "../include/ipc_interface.h"
/*
typedef struct ClientThreadArgs {
    volatile bool intentional_quit;
    int last_current_player_id;
    bool game_started;
    volatile bool wait_for_enter;
} ClientThreadArgs;
*/
static int parse_input(const char* input, int* out_count, int* out_value) {
    /*
    int count;
    char card_char;
    if (sscanf(input, "%d %c", &count, &card_char) != 2) {
        pkt.type = (MessageType)0;  // <- INIT NA 0 NAMIESTO -1
        return pkt;
    }
    
    if (value != -1 && count > 0) {
        pkt.type = MSG_BET;
        pkt.count = count;
        pkt.card_value = value;
    } else {
        pkt.type = (MessageType)0;  // <- INIT NA 0 NAMIESTO -1
    }
    return pkt;
*/
    if (strcasecmp(input, "liar") == 0) return 1;
    if (strcasecmp(input, "quit") == 0) return 2;

    char card_char;
    if (sscanf(input, "%d %c", out_count, &card_char) == 2) {
        switch (toupper(card_char)) {
            case 'Q':
                *out_value = CARD_QUEEN;
                return 3;
            case 'K':
                *out_value = CARD_KING;
                return 3;
            case 'A':
                *out_value = CARD_ACE;
                return 3;
            case 'J':
                *out_value = CARD_JOKER;
                return 3;
        }
    }
    return 0;
}
/*
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
}
    */

/*
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
*/

void* network_thread_func(void* args) {
    ClientState* state = (ClientState*) args;
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
    int bet_count;
    int bet_val;

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
                snprintf(state->last_message, sizeof(state->last_message), "❌ Invalid command. Try '2 K' or 'liar'.");
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

    while(1) {
        ui_show_welcome();
        printf(BOLD "Menu:" RESET "\n");
        printf(YELLOW "1." RESET " New game\n");
        printf(YELLOW "2." RESET " Join game\n");
        printf(YELLOW "3." RESET " Exit\n");
        printf(YELLOW "4." RESET " Game rules\n\n");

        int choice = ui_get_int("Choice: ", 1, 4, 0);
        if (choice == 3) {
            printf(CYAN "Goodbye!\n" RESET);
            break;
        } else if (choice == 4) {
            ui_show_rules();
            continue;
        } else if (choice == 1 || choice == 2) {
            if (!client_connect(&state, "127.0.0.1", true)) {
                printf(RED "❌ Could not connect to server.\n" RESET);
                ui_wait_enter();
                continue;
            }

            if (choice == 1) {
                int players = ui_get_int("Number of players (2-4): ", 2, 4, 2);
                int lives = ui_get_int("Initial lives (1-5): ", 1, 5, 3);
                client_send_join(&state, 0, lives, players);
            } else {
                int gid = ui_get_int("Enter Game ID: ", 1, 9999, 0);
                if (gid <= 0 || gid >= 10000) {
                    printf(RED "❌ Invalid ID (must be > 0 and < 9999).\n" RESET);
                    continue;
                }
                client_send_join(&state, gid, 0, 0);
            }

            run_game_loop(&state);
            client_disconnect(&state);
            printf(RESET "\nReturning to menu...\n");
            sleep(1);
        } else {
            printf(RED "Invalid choice.\n" RESET);
            break;
        }

        printf(BOLD YELLOW "\n╔════════════════════════════════╗\n");
        printf("║        🏆 GAME OVER 🏆         ║\n");
        printf("╚════════════════════════════════╝" RESET "\n");

        printf(CYAN "\nReturning to main menu...\n\n" RESET);
    }

    client_state_destroy(&state);
    return 0;
}