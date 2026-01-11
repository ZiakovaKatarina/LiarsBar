#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/server_game.h"

typedef struct {
    ServerGame games[MAX_GAMES];
    pthread_mutex_t mutex;
    IPC_Interface ipc;
    volatile int running;
} ServerState;

ServerState g_server;

typedef struct {
    int client_fd;
} ClientThreadArgs;

ServerGame* find_or_create_game(int game_id, int max_players,
                                int requested_lives) {
    pthread_mutex_lock(&g_server.mutex);

    ServerGame* target_game = NULL;

    if (game_id == 0) {
        for (int i = 0; i < MAX_GAMES; i++) {
            if (!g_server.games[i].is_running) {
                int new_id = 1000 + i;
                game_init(&g_server.games[i], new_id, max_players,
                          requested_lives, g_server.ipc);
                target_game = &g_server.games[i];
                printf("SERVER: Created new game #%d (Lives: %d)\n", new_id,
                       requested_lives);
                break;
            }
        }
    } else {
        for (int i = 0; i < MAX_GAMES; i++) {
            if (g_server.games[i].is_running &&
                g_server.games[i].game_id == game_id) {
                target_game = &g_server.games[i];
                break;
            }
        }
    }

    pthread_mutex_unlock(&g_server.mutex);
    return target_game;
}

void* client_handler(void* arg) {
    ClientThreadArgs* args = (ClientThreadArgs*)arg;
    int fd = args->client_fd;
    free(args);

    IPC_Interface ipc = g_server.ipc;
    GamePacket pkt;

    if (ipc.receive_packet(fd, &pkt) <= 0 || pkt.type != MSG_JOIN) {
        ipc.close_conn(fd);
        return NULL;
    }

    int requested_gid = pkt.game_id;
    int requested_lives = (pkt.count > 0) ? pkt.count : 3;
    int requested_players = (pkt.player_id > 0) ? pkt.player_id : MAX_PLAYERS;

    ServerGame* game =
        find_or_create_game(requested_gid, requested_players, requested_lives);

    if (!game) {
        GamePacket err = {.type = MSG_UPDATE};
        strcpy(err.text, "❌ Game not found or server full.");
        ipc.send_packet(fd, &err);
        ipc.close_conn(fd);
        return NULL;
    }

    int final_lives;
    if (requested_gid == 0) {
        final_lives = requested_lives;
    } else {
        final_lives = game->initial_lives;
    }

    int player_idx = game_add_player(game, fd, final_lives);

    if (player_idx == -1) {
        GamePacket err = {.type = MSG_UPDATE};
        strcpy(err.text, "❌ Game is full.");
        ipc.send_packet(fd, &err);
        ipc.close_conn(fd);
        return NULL;
    }

    GamePacket welcome = {.type = MSG_WELCOME};
    welcome.game_id = game->game_id;
    welcome.player_id = game->players[player_idx].id;
    for (int i = 0; i < MAX_PLAYERS; i++)
        welcome.lives[i] = game->players[i].lives;
    ipc.send_packet(fd, &welcome);

    while (g_server.running) {
        int res = ipc.receive_packet(fd, &pkt);
        if (res <= 0 || pkt.type == MSG_QUIT) break;

        pthread_mutex_lock(&game->mutex);
        if (game->players[player_idx].is_active) {
            if (pkt.type == MSG_BET)
                game_process_bet(game, player_idx, pkt.count, pkt.card_value);
            else if (pkt.type == MSG_LIAR)
                game_process_liar(game, player_idx);
        }
        pthread_mutex_unlock(&game->mutex);
    }

    pthread_mutex_lock(&game->mutex);
    game_remove_player(game, player_idx);
    if (game->connected_count == 0) {
        game->is_running = false;
        printf("SERVER: Game #%d closed (empty).\n", game->game_id);
    }
    pthread_mutex_unlock(&game->mutex);

    ipc.close_conn(fd);
    return NULL;
}

void sig_handler(int sig) {
    (void)sig;
    printf("\nSERVER: Shutting down...\n");
    g_server.running = 0;
    exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    int port = 9999;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    g_server.ipc = get_socket_interface();
    g_server.running = 1;
    pthread_mutex_init(&g_server.mutex, NULL);

    for (int i = 0; i < MAX_GAMES; i++) {
        g_server.games[i].is_running = false;
    }

    int server_fd = g_server.ipc.init_server(port);
    if (server_fd < 0) {
        fprintf(stderr, "Failed to start server on port %d.\n", port);
        return 1;
    }

    printf("╔════════════════════════════════╗\n");
    printf("║    SERVER RUNNING ON %d      ║\n", port);
    printf("╚════════════════════════════════╝\n");

    while (g_server.running) {
        int client_fd = g_server.ipc.accept_client(server_fd);
        if (client_fd < 0) continue;

        ClientThreadArgs* args = malloc(sizeof(ClientThreadArgs));
        if (!args) {
            close(client_fd);
            continue;
        }
        args->client_fd = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, args) != 0) {
            perror("Thread creation failed");
            free(args);
            close(client_fd);
        } else {
            pthread_detach(tid);
        }
    }

    g_server.ipc.close_conn(server_fd);
    pthread_mutex_destroy(&g_server.mutex);
    return 0;
}
