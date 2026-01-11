#include "../include/common.h"
#include "../include/ipc_interface.h"
#include "../include/logic.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

typedef struct {
    int game_id;
    int sockets[MAX_PLAYERS];
    int connected_players_count;
    int max_players;
    pthread_mutex_t mutex;
    
    int lives[MAX_PLAYERS];
    int player_cards[MAX_PLAYERS][5];
    int current_player;
    int current_bet_count;
    int current_bet_value;
    int last_bettor;
    int round_active;
    bool active;
} GameInstance;

typedef struct {
    int fd;
    GameInstance *instance;
    int player_id;
} ThreadArgs;

typedef struct {
    GameInstance games[MAX_GAMES];
    pthread_mutex_t global_mutex;
    IPC_Interface server_ipc;
    int next_game_id;
} ServerState;

static volatile sig_atomic_t server_running = 1;

void init_games(ServerState *state) {
    state->server_ipc = get_socket_interface();
    state->next_game_id = 1000;
    pthread_mutex_init(&state->global_mutex, NULL);
    
    for (int i = 0; i < MAX_GAMES; i++) {
        state->games[i].active = false;
        state->games[i].game_id = i + 1;
        state->games[i].max_players = MAX_PLAYERS;
        pthread_mutex_init(&state->games[i].mutex, NULL);
        for (int j = 0; j < MAX_PLAYERS; j++) {
            state->games[i].sockets[j] = -1;
        }
        state->games[i].current_player = -1;
    }
}

GameInstance* create_new_game(ServerState *state, int max_players) {
    pthread_mutex_lock(&state->global_mutex);
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!state->games[i].active) {
            memset(&state->games[i], 0, sizeof(GameInstance));
            state->games[i].active = true;
            state->games[i].game_id = i + 1;
            state->games[i].max_players = max_players;
            state->games[i].connected_players_count = 0;
            state->games[i].round_active = 0;
            state->games[i].current_player = -1;
            state->games[i].last_bettor = -1;
            state->games[i].current_bet_count = 0;
            state->games[i].current_bet_value = -1;
            pthread_mutex_init(&state->games[i].mutex, NULL);
            pthread_mutex_unlock(&state->global_mutex);
            return &state->games[i];
        }
    }
    pthread_mutex_unlock(&state->global_mutex);
    return NULL;
}

GameInstance* find_game(ServerState *state, int game_id) {
    pthread_mutex_lock(&state->global_mutex);
    for (int i = 0; i < MAX_GAMES; i++) {
        if (state->games[i].active && state->games[i].game_id == game_id) {
            pthread_mutex_unlock(&state->global_mutex);
            return &state->games[i];
        }
    }
    pthread_mutex_unlock(&state->global_mutex);
    return NULL;
}

void broadcast(GameInstance *inst, GamePacket *pkt, IPC_Interface ipc) {
    pkt->game_id = inst->game_id;
    for (int i = 0; i < inst->max_players; i++) {
        if (inst->sockets[i] != -1) {
            ipc.send_packet(inst->sockets[i], pkt);
        }
    }
}

void send_game_update(GameInstance *inst, IPC_Interface ipc,
                      const char* message, int bet_count, 
                      int bet_value, int player_id) {
    GamePacket pkt = {0};
    pkt.MessageType = MSG_UPDATE;
    pkt.game_id = inst->game_id;
    strcpy(pkt.text, message);
    pkt.count = bet_count;
    pkt.card_value = bet_value;
    pkt.current_player_id = player_id;
    memcpy(pkt.lives, inst->lives, sizeof(inst->lives));
    broadcast(inst, &pkt, ipc);
}

void start_new_round(GameInstance *inst, IPC_Interface ipc) {
    if (inst->round_active) return;

    inst->round_active = 1;
    inst->current_bet_count = 0;
    inst->current_bet_value = -1;
    inst->last_bettor = -1;

    for (int p = 0; p < MAX_PLAYERS; p++) {
        for (int c = 0; c < 5; c++) {
            inst->player_cards[p][c] = -1;
        }
    }

    inst->current_player = next_player(inst->current_player, inst->lives);

    if (inst->current_player == -1) {
        inst->round_active = 0;
        return;
    }
    
    deal_cards_to_all(inst->player_cards, inst->sockets, ipc, inst->lives, inst->current_player);

    printf(BLUE "[SERVER %d]" RESET " " YELLOW "🎯 New round! Player %d starts." RESET "\n", 
           inst->game_id, inst->current_player + 1);

    char msg[128];
    sprintf(msg, YELLOW "🎯 New round! Player %d starts." RESET, inst->current_player + 1);
    send_game_update(inst, ipc, msg, 0, -1, inst->current_player + 1);
}

void handle_invalid_join(IPC_Interface ipc, int fd, const char* message) {
    GamePacket err = {0};
    err.MessageType = MSG_UPDATE;
    strcpy(err.text, message);
    ipc.send_packet(fd, &err);
}

void send_welcome_packet(GameInstance *inst, IPC_Interface ipc, 
                        ThreadArgs *ta, int my_id, int initial_lives) {
    GamePacket welcome = {0};
    welcome.MessageType = MSG_WELCOME;
    welcome.player_id = my_id;
    welcome.game_id = inst->game_id;
    sprintf(welcome.text, "Welcome! You are Player %d in game %d.", my_id, inst->game_id);
    memcpy(welcome.lives, inst->lives, sizeof(welcome.lives));
    ipc.send_packet(ta->fd, &welcome);
}

void send_wait_packet(GameInstance *inst, IPC_Interface ipc) {
    int shown = inst->connected_players_count;
    if (shown > inst->max_players) shown = inst->max_players;

    GamePacket wait_pkt = {0};
    wait_pkt.MessageType = MSG_UPDATE;
    wait_pkt.game_id = inst->game_id;
    if (inst->connected_players_count < inst->max_players) {
        sprintf(wait_pkt.text, YELLOW "⏳ Waiting for players... (%d/%d)" RESET, shown, inst->max_players);
    } else {
        sprintf(wait_pkt.text, GREEN "✅ Enough players! (%d/%d) – Game starts!" RESET, shown, inst->max_players);
    }
    memcpy(wait_pkt.lives, inst->lives, sizeof(inst->lives));
    broadcast(inst, &wait_pkt, ipc);
}

void handle_bet_message(GameInstance *inst, IPC_Interface ipc, 
                       ThreadArgs *ta, int my_id, GamePacket *pkt) {
    const char* card_names[] = {"Q", "K", "A", "J"};
    int new_count = pkt->count;
    int new_value = pkt->card_value;

    if (!is_valid_bet(inst->current_bet_count, inst->current_bet_value, new_count, new_value)) {
        GamePacket err = {0};
        err.MessageType = MSG_UPDATE;
        err.game_id = inst->game_id;
        sprintf(err.text, RED "❌ Bet must be higher!" RESET);
        memcpy(err.lives, inst->lives, sizeof(err.lives));
        err.current_player_id = inst->current_player + 1;
        err.count = inst->current_bet_count;
        err.card_value = inst->current_bet_value;
        ipc.send_packet(ta->fd, &err);
        return;
    }

    int total_cards = count_total_cards(inst->lives);
    
    if (new_count > total_cards) {
        GamePacket err = {0};
        err.MessageType = MSG_UPDATE;
        err.game_id = inst->game_id;
        sprintf(err.text, RED "❌ Bet is higher than total cards (%d)!" RESET, total_cards);
        memcpy(err.lives, inst->lives, sizeof(err.lives));
        err.current_player_id = inst->current_player + 1;
        err.count = inst->current_bet_count;
        err.card_value = inst->current_bet_value;
        ipc.send_packet(ta->fd, &err);
        return;
    }

    inst->current_bet_count = new_count;
    inst->current_bet_value = new_value;
    inst->last_bettor = my_id;

    printf(BLUE "[SERVER %d]" RESET " " YELLOW "💰 Player %d: %d x %s\n" RESET,
           inst->game_id, my_id, new_count, card_names[new_value]);

    GamePacket up = {0};
    up.MessageType = MSG_UPDATE;
    up.game_id = inst->game_id;
    sprintf(up.text, YELLOW "💰 Player %d bets: %d x %s" RESET, my_id, new_count, card_names[new_value]);
    up.count = new_count;
    up.card_value = new_value;
    up.current_player_id = my_id;
    memcpy(up.lives, inst->lives, sizeof(up.lives));
    broadcast(inst, &up, ipc);

    inst->current_player = next_player(inst->current_player, inst->lives);

    char msg[128];
    sprintf(msg, CYAN "➡️  Turn: Player %d" RESET, inst->current_player + 1);
    send_game_update(inst, ipc, msg, new_count, new_value, inst->current_player + 1);
}

void handle_liar_message(GameInstance *inst, IPC_Interface ipc, int my_id) {
    const char* card_names[] = {"Q", "K", "A", "J"};
    
    if (inst->current_bet_count == 0 || inst->current_bet_value == -1) {
        printf(BLUE "[SERVER %d]" RESET " " RED "❌ Player %d called LIAR with no bet – loses life!\n" RESET, 
               inst->game_id, my_id);
        
        if (my_id > 0 && my_id <= MAX_PLAYERS) {
            inst->lives[my_id - 1]--;
        }
        
        GamePacket result_pkt = {0};
        result_pkt.MessageType = MSG_UPDATE;
        result_pkt.game_id = inst->game_id;
        sprintf(result_pkt.text, RED "❌ Player %d called LIAR with no bet – loses a life!" RESET, my_id);
        memcpy(result_pkt.lives, inst->lives, sizeof(inst->lives));
        broadcast(inst, &result_pkt, ipc);
        
        int alive_count = 0, winner_id = -1;
        alive_count = count_alive_players(inst->lives, &winner_id);
        
        if (alive_count <= 1) {
            GamePacket game_over_pkt = {0};
            game_over_pkt.MessageType = MSG_GAME_OVER;
            game_over_pkt.game_id = inst->game_id;
            if (alive_count == 1) {
                sprintf(game_over_pkt.text, GREEN "🏆 Player %d won the game!" RESET, winner_id + 1);
            } else {
                strcpy(game_over_pkt.text, YELLOW "🏁 Game ended – all players eliminated." RESET);
            }
            broadcast(inst, &game_over_pkt, ipc);
            inst->round_active = 0;
        } else {
            inst->round_active = 0;
            start_new_round(inst, ipc);
        }
        return;
    }
    
    int loser_id = -1;
    bool liar_succeeds = false;

    evaluate_liar(inst->player_cards, inst->lives, 
                 inst->current_bet_value, inst->current_bet_count,
                 inst->last_bettor, my_id, &loser_id, &liar_succeeds);

    if (loser_id > 0 && loser_id <= MAX_PLAYERS) {
        inst->lives[loser_id - 1]--;
    }

    int total_count = count_cards(inst->player_cards, inst->lives, inst->current_bet_value);

    printf(BLUE "[SERVER %d]" RESET " " RED "🎭 Player %d CALLS LIAR!" RESET "\n", 
           inst->game_id, my_id);
    printf(BLUE "[SERVER %d]" RESET " " YELLOW "📊 Was: %d x %s. Bet: %d x %s." RESET "\n",
           inst->game_id, total_count, card_names[inst->current_bet_value], 
           inst->current_bet_count, card_names[inst->current_bet_value]);

    GamePacket result_pkt = {0};
    result_pkt.MessageType = MSG_UPDATE;
    result_pkt.game_id = inst->game_id;
    if (liar_succeeds) {
        sprintf(result_pkt.text, GREEN "🎭 Liar succeeded! Only %d x %s. Player %d loses life." RESET,
                total_count, card_names[inst->current_bet_value], loser_id);
    } else {
        sprintf(result_pkt.text, RED "🎭 Liar failed! Was %d x %s. Player %d loses life." RESET,
                total_count, card_names[inst->current_bet_value], loser_id);
    }
    memcpy(result_pkt.lives, inst->lives, sizeof(inst->lives));
    broadcast(inst, &result_pkt, ipc);

    int alive_count = 0, winner_id = -1;
    alive_count = count_alive_players(inst->lives, &winner_id);

    if (alive_count <= 1) {
        GamePacket game_over_pkt = {0};
        game_over_pkt.MessageType = MSG_GAME_OVER;
        game_over_pkt.game_id = inst->game_id;
        if (alive_count == 1) {
            sprintf(game_over_pkt.text, GREEN "🏆 Player %d won the game!" RESET, winner_id + 1);
        } else {
            strcpy(game_over_pkt.text, YELLOW "🏁 Game ended – all players eliminated." RESET);
        }
        broadcast(inst, &game_over_pkt, ipc);
        inst->round_active = 0;
    } else {
        inst->round_active = 0;
        start_new_round(inst, ipc);
    }
}

void handle_disconnect(GameInstance *inst, IPC_Interface ipc, ThreadArgs *ta) {
    int player_index = ta->player_id - 1;
    inst->sockets[player_index] = -1;
    inst->lives[player_index] = 0;
    inst->connected_players_count--;
    
    printf(BLUE "[SERVER %d]" RESET " " RED "🔌 Player %d left (%d/%d).\n" RESET,
           inst->game_id, ta->player_id, inst->connected_players_count, inst->max_players);

    GamePacket disc_pkt = {0};
    disc_pkt.MessageType = MSG_UPDATE;
    disc_pkt.game_id = inst->game_id;
    sprintf(disc_pkt.text, RED "🔌 Player %d left the game." RESET, ta->player_id);
    memcpy(disc_pkt.lives, inst->lives, sizeof(disc_pkt.lives));
    broadcast(inst, &disc_pkt, ipc);

    if (inst->round_active && inst->current_player == player_index) {
        int next_p = next_player(inst->current_player, inst->lives);

        if (next_p == -1 || inst->lives[next_p] <= 0) {
            inst->round_active = 0;
        } else {
            inst->current_player = next_p;
            GamePacket turn_pkt = {0};
            turn_pkt.MessageType = MSG_UPDATE;
            turn_pkt.game_id = inst->game_id;
            sprintf(turn_pkt.text, CYAN "➡️  Turn: Player %d" RESET, inst->current_player + 1);
            turn_pkt.current_player_id = inst->current_player + 1;
            memcpy(turn_pkt.lives, inst->lives, sizeof(turn_pkt.lives));
            broadcast(inst, &turn_pkt, ipc);
        }
    }

    int alive_count = 0, winner_id = -1;
    alive_count = count_alive_players(inst->lives, &winner_id);

    if (alive_count <= 1 && inst->round_active) {
        GamePacket game_over_pkt = {0};
        game_over_pkt.MessageType = MSG_GAME_OVER;
        game_over_pkt.game_id = inst->game_id;
        if (alive_count == 1) {
            sprintf(game_over_pkt.text, GREEN "🏆 Player %d won the game!" RESET, winner_id + 1);
        } else {
            strcpy(game_over_pkt.text, YELLOW "🏁 Game ended – all players eliminated." RESET);
        }
        broadcast(inst, &game_over_pkt, ipc);
        inst->round_active = 0;
    }

    if (inst->connected_players_count == 0) {
        inst->active = false;
        printf(BLUE "[SERVER]" RESET " " MAGENTA "❌ Game %d CLOSED (no players).\n" RESET, inst->game_id);
    }
}

void* handle_client(void* arg) {
    void **args_array = (void**)arg;
    ThreadArgs *ta = (ThreadArgs*)args_array[0];
    ServerState *state = (ServerState*)args_array[1];
    IPC_Interface server_ipc = state->server_ipc;
    
    int my_id = -1;
    GameInstance *instance = NULL;

    GamePacket join_pkt = {0};
    int res = server_ipc.receive_packet(ta->fd, &join_pkt);
    if (res <= 0 || join_pkt.MessageType != MSG_JOIN) {
        printf(BLUE "[SERVER]" RESET " " RED "❌ Invalid join packet.\n" RESET);
        server_ipc.close_conn(ta->fd);
        free(ta);
        free(args_array);
        return NULL;
    }

    int requested_game_id = join_pkt.game_id;
    int max_players = (join_pkt.player_id > 0 && join_pkt.player_id <= MAX_PLAYERS) ? join_pkt.player_id : MAX_PLAYERS;

    if (requested_game_id == 0) {
        instance = create_new_game(state, max_players);
        if (!instance) {
            handle_invalid_join(server_ipc, ta->fd, 
                              "❌ Server is full – max 10 games.");
            printf(BLUE "[SERVER]" RESET " " RED "❌ Server is full (max games = %d).\n" RESET, MAX_GAMES);
            server_ipc.close_conn(ta->fd);
            free(ta);
            free(args_array);
            return NULL;
        }

        printf(BLUE "[SERVER]" RESET " " GREEN "✨ Created new game: ID %d (max players: %d)\n" RESET,
               instance->game_id, max_players);
    } else {
        instance = find_game(state, requested_game_id);
        if (!instance) {
            GamePacket err = {0};
            err.MessageType = MSG_UPDATE;
            err.game_id = requested_game_id;
            sprintf(err.text, RED "❌ Game %d does not exist." RESET, requested_game_id);
            server_ipc.send_packet(ta->fd, &err);
            printf(BLUE "[SERVER]" RESET " " RED "❌ Player wants game %d – DOES NOT EXIST.\n" RESET, requested_game_id);
            server_ipc.close_conn(ta->fd);
            free(ta);
            free(args_array);
            return NULL;
        }
        max_players = instance->max_players;
        printf(BLUE "[SERVER %d]" RESET " " GREEN "🔗 Player joining existing game.\n" RESET,
               instance->game_id);
    }

    ta->instance = instance;
    pthread_mutex_lock(&instance->mutex);
    if (instance->connected_players_count >= instance->max_players) {
        GamePacket err = {0};
        err.MessageType = MSG_UPDATE;
        err.game_id = instance->game_id;
        sprintf(err.text, RED "❌ Game is full (max %d players)." RESET, instance->max_players);
        server_ipc.send_packet(ta->fd, &err);
        printf(BLUE "[SERVER %d]" RESET " " RED "❌ Game is full.\n" RESET, instance->game_id);
        pthread_mutex_unlock(&instance->mutex);
        server_ipc.close_conn(ta->fd);
        free(ta);
        free(args_array);
        return NULL;
    }

    instance->connected_players_count++;
    my_id = instance->connected_players_count;
    instance->sockets[my_id - 1] = ta->fd;
    
    int initial_lives;
    if (requested_game_id == 0) {
        initial_lives = (join_pkt.count > 0 && join_pkt.count <= 5) ? join_pkt.count : 3;
        printf(BLUE "[SERVER %d]" RESET " " GREEN "✨ Game created with %d lives.\n" RESET,
               instance->game_id, initial_lives);
    } else {
        initial_lives = instance->lives[0] > 0 ? instance->lives[0] : 3;
    }
    
    instance->lives[my_id - 1] = initial_lives;
    ta->player_id = my_id;

    printf(BLUE "[SERVER %d]" RESET " " GREEN "🔗 Player %d connected (%d/%d, lives: %d).\n" RESET,
           instance->game_id, my_id, instance->connected_players_count, instance->max_players, initial_lives);

    send_welcome_packet(instance, server_ipc, ta, my_id, initial_lives);
    send_wait_packet(instance, server_ipc);

    if (instance->connected_players_count >= instance->max_players && instance->round_active == 0) {
        start_new_round(instance, server_ipc);
    }
    pthread_mutex_unlock(&instance->mutex);

    GamePacket pkt;
    while (1) {
        res = server_ipc.receive_packet(ta->fd, &pkt);
        if (res <= 0) {
            printf(BLUE "[SERVER %d]" RESET " " YELLOW "⚠️  Player %d: connection lost.\n" RESET, 
                   instance->game_id, my_id);
            break;
        }

        if (pkt.MessageType == MSG_QUIT) {
            printf(BLUE "[SERVER %d]" RESET " " YELLOW "⚠️  Player %d: requested disconnect.\n" RESET, 
                   instance->game_id, my_id);
            break;
        }

        pthread_mutex_lock(&instance->mutex);

        if (!instance->round_active) {
            pthread_mutex_unlock(&instance->mutex);
            continue;
        }

        int current_id = instance->current_player + 1;

        if (pkt.MessageType == MSG_BET) {
            if (my_id != current_id) {
                pthread_mutex_unlock(&instance->mutex);
                continue;
            }
            
            handle_bet_message(instance, server_ipc, ta, my_id, &pkt);
            pthread_mutex_unlock(&instance->mutex);
            continue;
        }

        if (pkt.MessageType == MSG_LIAR) {
            handle_liar_message(instance, server_ipc, my_id);
            pthread_mutex_unlock(&instance->mutex);
            continue;
        }

        pthread_mutex_unlock(&instance->mutex);
    }

    pthread_mutex_lock(&instance->mutex);
    handle_disconnect(instance, server_ipc, ta);
    pthread_mutex_unlock(&instance->mutex);
    
    server_ipc.close_conn(ta->fd);
    free(ta);
    free(args_array);
    return NULL;
}

static void signal_handler(int sig) {
    (void)sig;
    server_running = 0;
    printf(BLUE "\n[SERVER] Signal caught – shutting down...\n" RESET);
}

int main() {
    srand(time(NULL));

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    ServerState *state = malloc(sizeof(ServerState));
    if (!state) {
        printf(RED "❌ Memory allocation error.\n" RESET);
        return 1;
    }
    
    init_games(state);

    int s_fd = state->server_ipc.init_server();
    if (s_fd < 0) {
        printf(RED "❌ Failed to initialize server.\n" RESET);
        free(state);
        return 1;
    }

    int flags = fcntl(s_fd, F_GETFL, 0);
    fcntl(s_fd, F_SETFL, flags | O_NONBLOCK);

    printf(BLUE "[SERVER]" RESET " " GREEN BOLD "✅ Server running on port %d\n" RESET, PORT);
    printf(BLUE "[SERVER]" RESET " " YELLOW "⏳ Waiting for clients...\n" RESET);
    printf(BLUE "[SERVER]" RESET " " MAGENTA "Max games: %d | Max players/game: %d\n\n" RESET, MAX_GAMES, MAX_PLAYERS);

    while (server_running) {
        int c_fd = accept(s_fd, NULL, NULL);
        if (c_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(50000);
                continue;
            }
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        void **args = malloc(2 * sizeof(void*));
        ThreadArgs *ta = malloc(sizeof(ThreadArgs));
        if (!ta || !args) {
            printf(RED "❌ Memory allocation error.\n" RESET);
            close(c_fd);
            free(ta);
            free(args);
            continue;
        }

        ta->fd = c_fd;
        ta->instance = NULL;
        ta->player_id = -1;
        
        args[0] = ta;
        args[1] = state;

        pthread_t t;
        if (pthread_create(&t, NULL, handle_client, args) != 0) {
            printf(RED "❌ Thread creation error.\n" RESET);
            free(ta);
            free(args);
            close(c_fd);
            continue;
        }
        pthread_detach(t);
    }

    close(s_fd);
    
    pthread_mutex_destroy(&state->global_mutex);
    for (int i = 0; i < MAX_GAMES; i++) {
        pthread_mutex_destroy(&state->games[i].mutex);
    }
    
    free(state);
    printf(BLUE "[SERVER] Server correctly terminated.\n" RESET);
    return 0;
}