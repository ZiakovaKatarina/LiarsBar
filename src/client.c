#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
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
                return 3;
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

            if (strstr(state->last_message, "❌") == NULL) {
                strcpy(state->last_message, "⚠️ Disconnected from server.");
            }

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

int main(int argc, char* argv[]) {
    int port = 9999;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    IPC_Interface ipc = get_socket_interface();
    ClientState state;

    client_state_init(&state, ipc);

    while (1) {
        ui_print_welcome();
        printf("(Current Port: %d)\n\n", port);
        printf("1. New Game\n");
        printf("2. Join Game\n");
        printf("3. Rules\n");
        printf("4. Exit\n\n");

        int choice = ui_get_int("Choice: ", 1, 4, 0);

        if (choice == 3) {
            ui_print_rules();
            continue;
        }
        if (choice == 4) break;

        if (!client_connect(&state, "127.0.0.1", port, true)) {
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
            if (gid == 0) continue;
            client_send_join(&state, gid, 0, 0);
        }

        run_game_loop(&state);

        client_disconnect(&state);
        printf(RESET "\nReturning to menu...\n");
        sleep(1);
    }

    client_state_destroy(&state);
    return 0;
}
