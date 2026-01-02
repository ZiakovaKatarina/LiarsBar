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
    switch(value) {
        case CARD_QUEEN: printf("Q"); break;
        case CARD_KING: printf("K"); break;
        case CARD_ACE: printf("A"); break;
        case CARD_JOKER: printf("J"); break;
        default: printf("?"); break;
    }
}

void wait_for_enter(void) {
    printf(CYAN "\nStlač Enter pre návrat do hlavného menu... " RESET);
    fflush(stdout);
    
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void print_game_state(ClientThreadArgs *args) {
    if (args->current_player_id == 0) return;

    printf(BOLD BLUE "\n╔════════════════════════════════╗\n");
    printf("║          📊 STAV HRY           ║\n");
    printf("╚════════════════════════════════╝" RESET "\n");
    printf(MAGENTA "❤️  Životy:" RESET "\n");
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (args->lives[i] > 0 || i + 1 == args->player_id) {
            printf("   Hráč %d: ", i + 1);
            for (int j = 0; j < args->lives[i]; j++) printf(RED "❤️ " RESET);
            printf(MAGENTA " (%d)" RESET "\n", args->lives[i]);
        }
    }
    printf("\n");

    if (args->current_bet_count > 0) {
        printf(YELLOW "💰 Aktuálna stávka: %d x ", args->current_bet_count);
        print_card(args->current_bet_value);
        printf(RESET "\n");
    }

    printf(CYAN "➡️  Na ťahu:  Hráč %d" RESET, args->current_player_id);
    if (args->current_player_id == args->player_id) {
        printf(RED " " BOLD "◄── TY!" RESET "\n");
    } else {
        printf("\n");
    }

    printf(GREEN "📋 Tvoje karty: " RESET);
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
    args->game_started = false;
    args->wait_for_enter = false;
    args->last_current_player_id = 0;
    args->fd = -1;
}

GamePacket parse_bet_input(const char* input) {
    GamePacket pkt = {0};
    
    if (strcasecmp(input, "klamar") == 0) {
        pkt.MessageType = MSG_LIAR;
        return pkt;
    }
    
    int count;
    char card_char;
    if (sscanf(input, "%d %c", &count, &card_char) == 2) {
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
    } else {
        pkt.MessageType = -1;
    }
    return pkt;
}

void start_server_if_needed(ClientThreadArgs *args, IPC_Interface ipc) {
    args->fd = ipc.init_client("127.0.0.1");
    
    if (args->fd < 0) {
        printf(YELLOW "⚠️  Server nebeží - spúšťam ho automaticky...\n" RESET);
        
        if (system(SERVER_PATH " &") == -1) {
            printf(RED "❌ Nepodarilo sa spustiť server!\n" RESET);
            printf(YELLOW "Skús spustiť server manuálne:\n" RESET);
            printf(YELLOW "   ./server\n\n" RESET);
            args->fd = -1;
            return;
        }
        
        for (int i = 0; i < 10 && args->fd < 0; i++) {
            usleep(200000);
            args->fd = ipc.init_client("127.0.0.1");
        }
        if (args->fd < 0) {
            printf(RED "❌ Stále sa nedá pripojiť k serveru.\n" RESET);
            printf(YELLOW "Skús to znova alebo spusti server manuálne.\n\n" RESET);
            return;
        }
    }

    printf(CYAN "✅ Pripojené k serveru.\n" RESET);
}

void* receive_thread(void* arg) {
    ClientThreadArgs *args = (ClientThreadArgs*)arg;
    GamePacket pkt;

    while(args->is_running) {
        int res = args->ipc.receive_packet(args->fd, &pkt);

        if (res <= 0) {
            if (!args->intentional_quit) {
                printf(RED BOLD "\n╔════════════════════════════════════╗\n");
                printf("║     ⚠️   SERVER BOL UKONČENÝ ⚠️      ║\n");
                printf("╚════════════════════════════════════╝" RESET "\n");
                printf(YELLOW "Spojenie so serverom bolo prerušené.\n");
                printf("Server bol pravdepodobne vypnutý alebo spadol.\n" RESET);
                fflush(stdout);

                args->wait_for_enter = true;
            }
            args->is_running = 0;
            break;
        }

        switch(pkt.MessageType) {
            case MSG_WELCOME:
                args->player_id = pkt.player_id;
                args->game_id = pkt.game_id;
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                printf(CYAN "[SERVER]: 🎮 Vitaj! Si Hráč %d v hre %d." RESET "\n", pkt.player_id, pkt.game_id);
                break;

            case MSG_START_ROUND:
                args->game_started = true;
                printf(YELLOW "\n╔════════════════════════════════╗\n");
                printf("║        🎯 NOVÉ KOLO 🎯         ║\n");
                printf("╚════════════════════════════════╝" RESET "\n");
                printf(GREEN "📋 Tvoje karty: " RESET);
                for(int i = 0; i < MAX_LIVES; i++) {
                    if (pkt.my_cards[i] >= 0) {
                        printf(GREEN);
                        print_card(pkt.my_cards[i]);
                        printf(" " RESET);
                    }
                }
                printf("\n");

                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                memcpy(args->my_cards, pkt.my_cards, sizeof(pkt.my_cards));
                args->current_player_id = pkt.current_player_id;
                args->last_current_player_id = 0;
                args->current_bet_count = 0;
                args->current_bet_value = -1;
                break;

            case MSG_UPDATE: {
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                
                if (pkt.current_player_id != 0) {
                    args->current_player_id = pkt.current_player_id;
                }
                if (pkt.count > 0) {
                    args->current_bet_count = pkt.count;
                    args->current_bet_value = pkt.card_value;
                }

                bool is_fatal_error = (strstr(pkt.text, "neexistuje") != NULL) ||
                                      (strstr(pkt.text, "plná") != NULL) ||
                                      (strstr(pkt.text, "Server je plný") != NULL);

                if (strstr(pkt.text, "❌") != NULL && is_fatal_error) {
                    printf(RED "%s\n" RESET, pkt.text);
                    args->wait_for_enter = true;
                    args->is_running = 0;
                    break;
                }

                bool waiting = (args->current_player_id == 0 && args->current_bet_count == 0 && !args->game_started);
                if (waiting) {
                    printf("%s\n", pkt.text);
                    break;
                }

                printf(BLUE "[UPDATE]: %s" RESET "\n", pkt.text);

                if (args->current_player_id != args->last_current_player_id && args->current_player_id != 0) {
                    print_game_state(args);
                    args->last_current_player_id = args->current_player_id;
                    if (args->current_player_id == args->player_id) {
                        printf(BLUE "> " RESET);
                        fflush(stdout);
                    }
                } else if (strstr(pkt.text, "❌") != NULL && args->current_player_id == args->player_id) {
                    printf(BLUE "> " RESET);
                    fflush(stdout);
                }
                break;
            }

            case MSG_GAME_OVER:
                printf(BOLD YELLOW "\n╔════════════════════════════════╗\n");
                printf("║        🏆 KONIEC HRY 🏆        ║\n");
                printf("╚════════════════════════════════╝" RESET "\n");
                printf(GREEN "%s" RESET "\n", pkt.text);
                args->wait_for_enter = true;
                args->is_running = 0;
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
        if (ret == 0) {
            continue;
        }
        if (ret < 0) {
            break;
        }

        if (!fgets(input, sizeof(input), stdin)) {
            args->is_running = 0;
            break;
        }
        
        input[strcspn(input, "\n")] = 0;
        if (strlen(input) == 0) continue;

        if (strcmp(input, "quit") == 0) {
            printf(CYAN "\nOpúšťam hru a vraciam sa do menu...\n" RESET);
            args->intentional_quit = true;
            GamePacket quit_pkt = {.MessageType = MSG_QUIT};
            ipc.send_packet(args->fd, &quit_pkt);
            args->is_running = 0;
            ipc.close_conn(args->fd);
            break;
        }

        GamePacket pkt = parse_bet_input(input);
        if (pkt.MessageType == -1) {
            printf(RED "Nerozumiem príkazu. Skús: '3 K' alebo 'klamar'\n" RESET);
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
    printf(CYAN "\nNávrat do hlavného menu...\n\n" RESET);
}

void show_rules(void) {
    printf(BOLD YELLOW "\n╔══════════════════════════════════════════════════╗\n");
    printf("║              PRAVIDLÁ HRY LIAR'S BAR             ║\n");
    printf("╚══════════════════════════════════════════════════╝" RESET "\n");
    printf("- Hra pre " BOLD "2–4 hráčov" RESET " s balíčkom:\n");
    printf("      → 6× Q (kráľovná),\n      → 6× K (kráľ),\n      → 6× A (eso),\n      → 2× J (" BOLD "žolík" RESET ").\n");
    printf("- Každý hráč začína s " BOLD "3 životmi" RESET " (3 karty v ruke).\n");
    printf("- Hráči sa striedajú v stávkach na " BOLD "celkový počet a hodnotu kariet" RESET " na stole.\n");
    printf("- Príklad: \"5 K\" = aspoň 5 kráľov (vrátane žolíkov ako wildcard).\n");
    printf("- Poradie hodnôt: " BOLD "Q < K < A < J" RESET "\n");
    printf("- " BOLD "Žolík (J)" RESET " sa ráta ku všetkým hodnotám.\n");
    printf("- Nová stávka musí byť " BOLD "vyššia" RESET " ako predchádzajúca:\n");
    printf("      → väčší počet, alebo\n");
    printf("      → rovnaký počet a vyššia hodnota.\n");
    printf("- Hráč na ťahu môže buď stávkovať, alebo zavolať " BOLD "klamar" RESET "\n");
    printf("- Ak klamár " BOLD "uspeje" RESET " (bolo menej ako stávka) → stávkujúci stráca život.\n");
    printf("- Ak klamár " BOLD "neuspeje" RESET " (bolo aspoň toľko) → klamár stráca život.\n");
    printf("- Po strate života sa " BOLD "rozdajú nové karty" RESET " podľa aktuálnych životov.\n");
    printf("- Hra končí, keď ostane len jeden hráč s životmi → ten vyhrá.\n");
    printf("- Príkaz " BOLD "quit" RESET " kedykoľvek počas hry = návrat do menu.\n\n");
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
    } else {
        printf(YELLOW "⚠️  Neplatná hodnota, použijem predvolené (%d).\n" RESET, default_val);
        return default_val;
    }
}

char* get_game_id_input(void) {
    printf(YELLOW "Zadaj ID partie (alebo 'quit' pre návrat): " RESET);
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
        "\n👥 Koľko hráčov? (predvolené: %d, min: %d, max: %d): ",
        MIN_PLAYERS, MAX_PLAYERS, MAX_PLAYERS
    );
    printf(GREEN "✓ Hra pre %d hráčov.\n" RESET, max_players);

    int initial_lives = get_int_input(
        "💚 Koľko životov na začiatku? (predvolené: %d, min: %d, max: %d): ",
        1, MAX_LIVES, 3
    );
    printf(GREEN "✓ Hra začne s %d životmi pre každého hráča.\n\n" RESET, initial_lives);

    start_server_if_needed(&args, ipc);
    if (args.fd < 0) return;

    GamePacket join_pkt = {.MessageType = MSG_JOIN, .game_id = 0};
    join_pkt.count = initial_lives;
    join_pkt.player_id = max_players;
    strcpy(join_pkt.text, "Som hostiteľ");
    ipc.send_packet(args.fd, &join_pkt);

    game_loop(&args, ipc);
}

void handle_join_game(IPC_Interface ipc) {
    printf(GREEN "Pripájam sa k hre...\n" RESET);
    
    char* id_line = get_game_id_input();
    if (!id_line) return;
    
    if (strcmp(id_line, "quit") == 0 || strcmp(id_line, "q") == 0) {
        printf(CYAN "Návrat do hlavného menu...\n\n" RESET);
        return;
    }
    
    int game_id = atoi(id_line);
    if (game_id <= 0) {
        printf(RED "❌ Neplatné ID (musí byť > 0 alebo 'quit').\n\n" RESET);
        return;
    }

    ClientThreadArgs args;
    init_client_args(&args, ipc);

    args.fd = ipc.init_client("127.0.0.1");
    if (args.fd < 0) {
        printf(RED "❌ Nepodarilo sa pripojiť k serveru.\n\n" RESET);
        return;
    }
    
    GamePacket join_pkt = {.MessageType = MSG_JOIN, .game_id = game_id};
    join_pkt.count = 0;
    strcpy(join_pkt.text, "Pripojil som sa");
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
        printf(YELLOW "1." RESET " Nová hra\n");
        printf(YELLOW "2." RESET " Pripojenie k hre\n");
        printf(YELLOW "3." RESET " Koniec\n");
        printf(YELLOW "4." RESET " Pravidlá hry\n\n");
        printf(BLUE "Voľba: " RESET);
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
                printf(CYAN "Ádiós!\n" RESET);
                return 0;

            case '4':
                show_rules();
                break;

            default:
                printf(RED "Neplatná voľba.\n" RESET);
                break;
        }
    }

    return 0;
}