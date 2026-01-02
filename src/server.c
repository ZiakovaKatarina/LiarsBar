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

void init_games(ServerState *state) {
    state->server_ipc = get_socket_interface();
    state->next_game_id = 1000;
    pthread_mutex_init(&state->global_mutex, NULL);
    
    for (int i = 0; i < MAX_GAMES; i++) {
        state->games[i].active = false;
        state->games[i].game_id = 0;
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
            state->games[i].game_id = state->next_game_id++;
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

void start_new_round(GameInstance *inst, IPC_Interface ipc) {
    if (inst->round_active) return;

    inst->round_active = 1;
    inst->current_bet_count = 0;
    inst->current_bet_value = -1;
    inst->last_bettor = -1;

    int starter = inst->current_player;
    int attempts = 0;
    do {
        starter = (starter + 1) % inst->max_players;
        attempts++;
    } while (attempts < inst->max_players && inst->lives[starter] <= 0);

    if (attempts >= inst->max_players) {
        inst->round_active = 0;
        return;
    }

    inst->current_player = starter;
    
    rozdaj_karty_vsetkym(inst->player_cards, inst->sockets, ipc, inst->lives, inst->current_player);

    printf(BLUE "[SERVER %d]" RESET " " YELLOW "🎯 Nové kolo! Začína hráč %d." RESET "\n", 
           inst->game_id, starter + 1);

    GamePacket update_pkt = {0};
    update_pkt.MessageType = MSG_UPDATE;
    update_pkt.game_id = inst->game_id;
    sprintf(update_pkt.text, YELLOW "🎯 Nové kolo! Začína hráč %d." RESET, starter + 1);
    update_pkt.current_player_id = starter + 1;
    update_pkt.count = 0;
    update_pkt.card_value = -1;
    memcpy(update_pkt.lives, inst->lives, sizeof(inst->lives));
    broadcast(inst, &update_pkt, ipc);
}

void evaluate_liar(GameInstance *inst, int caller_id_1based, IPC_Interface ipc) {
    pthread_mutex_lock(&inst->mutex);
    
    if (inst->last_bettor == -1 || inst->last_bettor < 1 || inst->last_bettor > inst->max_players) {
        pthread_mutex_unlock(&inst->mutex);
        return;
    }

    int called_value = inst->current_bet_value;
    int bet_count = inst->current_bet_count;

    int total_count = 0;
    for (int p = 0; p < inst->max_players; p++) {
        if (inst->lives[p] > 0) {
            for (int c = 0; c < inst->lives[p]; c++) {
                if (inst->player_cards[p][c] == called_value || inst->player_cards[p][c] == CARD_JOKER) {
                    total_count++;
                }
            }
        }
    }

    bool liar_succeeds = (total_count < bet_count);
    int loser_id_1based = liar_succeeds ? inst->last_bettor : caller_id_1based;
    int loser_index = loser_id_1based - 1;

    const char* card_names[] = {"Q", "K", "A", "J"};
    printf(BLUE "[SERVER %d]" RESET " " RED "🎭 Hráč %d VOLÁ KLAMÁR!" RESET "\n", 
           inst->game_id, caller_id_1based);
    printf(BLUE "[SERVER %d]" RESET " " YELLOW "📊 Bolo: %d x %s. Stávka: %d x %s." RESET "\n",
           inst->game_id, total_count, card_names[called_value], bet_count, card_names[called_value]);

    inst->lives[loser_index]--;

    GamePacket result_pkt = {0};
    result_pkt.MessageType = MSG_UPDATE;
    result_pkt.game_id = inst->game_id;
    if (liar_succeeds) {
        sprintf(result_pkt.text, GREEN "🎭 Klamár uspel! Bolo len %d x %s. Hráč %d stráca život." RESET,
                total_count, card_names[called_value], loser_id_1based);
    } else {
        sprintf(result_pkt.text, RED "🎭 Klamár neuspel! Bolo %d x %s. Hráč %d stráca život." RESET,
                total_count, card_names[called_value], loser_id_1based);
    }
    memcpy(result_pkt.lives, inst->lives, sizeof(inst->lives));
    broadcast(inst, &result_pkt, ipc);

    int alive_count = 0, winner_id = -1;
    for (int i = 0; i < inst->max_players; i++) {
        if (inst->lives[i] > 0) { 
            alive_count++; 
            winner_id = i; 
        }
    }

    if (alive_count <= 1) {
        GamePacket game_over_pkt = {0};
        game_over_pkt.MessageType = MSG_GAME_OVER;
        game_over_pkt.game_id = inst->game_id;
        if (alive_count == 1) {
            sprintf(game_over_pkt.text, GREEN "🏆 Hráč %d vyhral partiu!" RESET, winner_id + 1);
        } else {
            strcpy(game_over_pkt.text, YELLOW "🏁 Hra skončila – všetci hráči vypadli." RESET);
        }
        broadcast(inst, &game_over_pkt, ipc);
        inst->round_active = 0;
    } else {
        inst->round_active = 0;
        start_new_round(inst, ipc);
    }
    
    pthread_mutex_unlock(&inst->mutex);
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
        printf(BLUE "[SERVER]" RESET " " RED "❌ Neplatný join packet.\n" RESET);
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
            GamePacket err = {0};
            err.MessageType = MSG_UPDATE;
            sprintf(err.text, RED "❌ Server je plný – max %d hier." RESET, MAX_GAMES);
            server_ipc.send_packet(ta->fd, &err);
            printf(BLUE "[SERVER]" RESET " " RED "❌ Server je plný (max hier = %d).\n" RESET, MAX_GAMES);
            server_ipc.close_conn(ta->fd);
            free(ta);
            free(args_array);
            return NULL;
        }

        printf(BLUE "[SERVER]" RESET " " GREEN "✨ Vytvorená nová hra: ID %d (max hráčov: %d)\n" RESET,
               instance->game_id, max_players);
    } else {
        instance = find_game(state, requested_game_id);
        if (!instance) {
            GamePacket err = {0};
            err.MessageType = MSG_UPDATE;
            err.game_id = requested_game_id;
            sprintf(err.text, RED "❌ Hra %d neexistuje." RESET, requested_game_id);
            server_ipc.send_packet(ta->fd, &err);
            printf(BLUE "[SERVER]" RESET " " RED "❌ Hráč chce partiu %d – NEEXISTUJE.\n" RESET, requested_game_id);
            server_ipc.close_conn(ta->fd);
            free(ta);
            free(args_array);
            return NULL;
        }
        max_players = instance->max_players;
        printf(BLUE "[SERVER %d]" RESET " " GREEN "🔗 Hráč sa pripája k existujúcej hre.\n" RESET,
               instance->game_id);
    }

    ta->instance = instance;
    pthread_mutex_lock(&instance->mutex);
    if (instance->connected_players_count >= instance->max_players) {
        GamePacket err = {0};
        err.MessageType = MSG_UPDATE;
        err.game_id = instance->game_id;
        sprintf(err.text, RED "❌ Hra je plná (max %d hráčov)." RESET, instance->max_players);
        server_ipc.send_packet(ta->fd, &err);
        printf(BLUE "[SERVER %d]" RESET " " RED "❌ Hra je plná.\n" RESET, instance->game_id);
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
        printf(BLUE "[SERVER %d]" RESET " " GREEN "✨ Hra vytvorená s %d životmi.\n" RESET,
               instance->game_id, initial_lives);
    } else {
        initial_lives = instance->lives[0] > 0 ? instance->lives[0] : 3;
    }
    
    instance->lives[my_id - 1] = initial_lives;
    ta->player_id = my_id;

    printf(BLUE "[SERVER %d]" RESET " " GREEN "🔗 Hráč %d sa pripojil (%d/%d, životy: %d).\n" RESET,
           instance->game_id, my_id, instance->connected_players_count, instance->max_players, initial_lives);

    GamePacket welcome = {0};
    welcome.MessageType = MSG_WELCOME;
    welcome.player_id = my_id;
    welcome.game_id = instance->game_id;
    sprintf(welcome.text, "Vitaj! Si Hráč %d v hre %d.", my_id, instance->game_id);
    memcpy(welcome.lives, instance->lives, sizeof(welcome.lives));
    server_ipc.send_packet(ta->fd, &welcome);

    int shown = instance->connected_players_count;
    if (shown > instance->max_players) shown = instance->max_players;

    GamePacket wait_pkt = {0};
    wait_pkt.MessageType = MSG_UPDATE;
    wait_pkt.game_id = instance->game_id;
    if (instance->connected_players_count < instance->max_players) {
        sprintf(wait_pkt.text, YELLOW "⏳ Čakáme na hráčov... (%d/%d)" RESET, shown, instance->max_players);
    } else {
        sprintf(wait_pkt.text, GREEN "✅ Máme dosť hráčov! (%d/%d) – Hra začína!" RESET, shown, instance->max_players);
    }
    memcpy(wait_pkt.lives, instance->lives, sizeof(wait_pkt.lives));
    broadcast(instance, &wait_pkt, server_ipc);

    if (instance->connected_players_count >= instance->max_players && instance->round_active == 0) {
        start_new_round(instance, server_ipc);
    }
    pthread_mutex_unlock(&instance->mutex);

    GamePacket pkt;
    while (1) {
        res = server_ipc.receive_packet(ta->fd, &pkt);
        if (res <= 0) {
            printf(BLUE "[SERVER %d]" RESET " " YELLOW "⚠️  Hráč %d: spojenie prerušené.\n" RESET, 
                   instance->game_id, my_id);
            break;
        }

        if (pkt.MessageType == MSG_QUIT) {
            printf(BLUE "[SERVER %d]" RESET " " YELLOW "⚠️  Hráč %d: žiadal disconnect.\n" RESET, 
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

            int new_count = pkt.count;
            int new_value = pkt.card_value;

            bool higher = (instance->current_bet_count == 0 && instance->current_bet_value == -1) ||
                         (new_count > instance->current_bet_count) ||
                         (new_count == instance->current_bet_count && new_value > instance->current_bet_value);

            if (!higher) {
                GamePacket err = {0};
                err.MessageType = MSG_UPDATE;
                err.game_id = instance->game_id;
                sprintf(err.text, RED "❌ Stávka musí byť vyššia!" RESET);
                memcpy(err.lives, instance->lives, sizeof(err.lives));
                err.current_player_id = current_id;
                err.count = instance->current_bet_count;
                err.card_value = instance->current_bet_value;
                server_ipc.send_packet(ta->fd, &err);
                pthread_mutex_unlock(&instance->mutex);
                continue;
            }

            int total_cards = 0;
            for (int i = 0; i < instance->max_players; i++) {
                if (instance->lives[i] > 0) total_cards += instance->lives[i];
            }
            
            if (new_count > total_cards) {
                GamePacket err = {0};
                err.MessageType = MSG_UPDATE;
                err.game_id = instance->game_id;
                sprintf(err.text, RED "❌ Stávka je vyššia ako celkom kariet (%d)!" RESET, total_cards);
                memcpy(err.lives, instance->lives, sizeof(err.lives));
                err.current_player_id = current_id;
                err.count = instance->current_bet_count;
                err.card_value = instance->current_bet_value;
                server_ipc.send_packet(ta->fd, &err);
                pthread_mutex_unlock(&instance->mutex);
                continue;
            }

            instance->current_bet_count = new_count;
            instance->current_bet_value = new_value;
            instance->last_bettor = my_id;

            const char* card_names[] = {"Q", "K", "A", "J"};
            printf(BLUE "[SERVER %d]" RESET " " YELLOW "💰 Hráč %d: %d x %s\n" RESET,
                   instance->game_id, my_id, new_count, card_names[new_value]);

            GamePacket up = {0};
            up.MessageType = MSG_UPDATE;
            up.game_id = instance->game_id;
            sprintf(up.text, YELLOW "💰 Hráč %d staví: %d x %s" RESET, my_id, new_count, card_names[new_value]);
            up.count = new_count;
            up.card_value = new_value;
            up.current_player_id = my_id;
            memcpy(up.lives, instance->lives, sizeof(up.lives));
            broadcast(instance, &up, server_ipc);

            do {
                instance->current_player = (instance->current_player + 1) % instance->max_players;
            } while (instance->lives[instance->current_player] <= 0);

            GamePacket turn = {0};
            turn.MessageType = MSG_UPDATE;
            turn.game_id = instance->game_id;
            sprintf(turn.text, CYAN "➡️  Na ťahu: Hráč %d" RESET, instance->current_player + 1);
            turn.current_player_id = instance->current_player + 1;
            turn.count = new_count;
            turn.card_value = new_value;
            memcpy(turn.lives, instance->lives, sizeof(turn.lives));
            broadcast(instance, &turn, server_ipc);

            pthread_mutex_unlock(&instance->mutex);
            continue;
        }

        if (pkt.MessageType == MSG_LIAR) {
            pthread_mutex_unlock(&instance->mutex);
            evaluate_liar(instance, my_id, server_ipc);
            continue;
        }

        pthread_mutex_unlock(&instance->mutex);
    }

    pthread_mutex_lock(&instance->mutex);
    int player_index = ta->player_id - 1;
    instance->sockets[player_index] = -1;
    instance->lives[player_index] = 0;
    instance->connected_players_count--;
    
    printf(BLUE "[SERVER %d]" RESET " " RED "🔌 Hráč %d odišiel (%d/%d).\n" RESET,
           instance->game_id, ta->player_id, instance->connected_players_count, instance->max_players);

    GamePacket disc_pkt = {0};
    disc_pkt.MessageType = MSG_UPDATE;
    disc_pkt.game_id = instance->game_id;
    sprintf(disc_pkt.text, RED "🔌 Hráč %d opustil hru." RESET, ta->player_id);
    memcpy(disc_pkt.lives, instance->lives, sizeof(disc_pkt.lives));
    broadcast(instance, &disc_pkt, server_ipc);

    if (instance->round_active && instance->current_player == player_index) {
        int next_player = instance->current_player;
        int tries = 0;
        do {
            next_player = (next_player + 1) % instance->max_players;
            tries++;
        } while (tries < instance->max_players && instance->lives[next_player] <= 0);

        if (tries >= instance->max_players || instance->lives[next_player] <= 0) {
            instance->round_active = 0;
        } else {
            instance->current_player = next_player;
            GamePacket turn_pkt = {0};
            turn_pkt.MessageType = MSG_UPDATE;
            turn_pkt.game_id = instance->game_id;
            sprintf(turn_pkt.text, CYAN "➡️  Na ťahu: Hráč %d" RESET, instance->current_player + 1);
            turn_pkt.current_player_id = instance->current_player + 1;
            memcpy(turn_pkt.lives, instance->lives, sizeof(turn_pkt.lives));
            broadcast(instance, &turn_pkt, server_ipc);
        }
    }

    int alive_count = 0, winner_id = -1;
    for (int i = 0; i < instance->max_players; i++) {
        if (instance->lives[i] > 0) { 
            alive_count++; 
            winner_id = i; 
        }
    }

    if (alive_count <= 1 && instance->round_active) {
        GamePacket game_over_pkt = {0};
        game_over_pkt.MessageType = MSG_GAME_OVER;
        game_over_pkt.game_id = instance->game_id;
        if (alive_count == 1) {
            sprintf(game_over_pkt.text, GREEN "🏆 Hráč %d vyhral partiu!" RESET, winner_id + 1);
        } else {
            strcpy(game_over_pkt.text, YELLOW "🏁 Hra skončila – všetci hráči vypadli." RESET);
        }
        broadcast(instance, &game_over_pkt, server_ipc);
        instance->round_active = 0;
    }

    if (instance->connected_players_count == 0) {
        instance->active = false;
        printf(BLUE "[SERVER]" RESET " " MAGENTA "❌ Partia %d ZATVORENÁ (bez hráčov).\n" RESET, instance->game_id);
    }

    pthread_mutex_unlock(&instance->mutex);
    server_ipc.close_conn(ta->fd);
    free(ta);
    free(args_array);
    return NULL;
}

int main() {
    srand(time(NULL));
    
    ServerState *state = malloc(sizeof(ServerState));
    if (!state) {
        printf(RED "❌ Chyba pri alokácii pamäte.\n" RESET);
        return 1;
    }
    
    init_games(state);

    int s_fd = state->server_ipc.init_server();
    if (s_fd < 0) {
        printf(RED "❌ Nepodarilo sa inicializovať server.\n" RESET);
        free(state);
        return 1;
    }

    printf(BLUE "[SERVER]" RESET " " GREEN BOLD "✅ Server beží na porte %d\n" RESET, PORT);
    printf(BLUE "[SERVER]" RESET " " YELLOW "⏳ Čakám na klientov...\n" RESET);
    printf(BLUE "[SERVER]" RESET " " MAGENTA "Max hier: %d | Max hráčov/partiu: %d\n\n" RESET, MAX_GAMES, MAX_PLAYERS);

    while (1) {
        int c_fd = accept(s_fd, NULL, NULL);
        if (c_fd < 0) {
            perror("accept");
            continue;
        }

        void **args = malloc(2 * sizeof(void*));
        ThreadArgs *ta = malloc(sizeof(ThreadArgs));
        if (!ta || !args) {
            printf(RED "❌ Chyba pri alokácii pamäte.\n" RESET);
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
            printf(RED "❌ Chyba pri vytváraní vlákna.\n" RESET);
            free(ta);
            free(args);
            close(c_fd);
            continue;
        }
        pthread_detach(t);
    }

    close(s_fd);
    free(state);
    return 0;
}