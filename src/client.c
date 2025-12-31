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

#define SERVER_PATH "./server"

typedef struct ClientThreadArgs {
    int fd;
    IPC_Interface ipc;
    volatile int is_running;
    int player_id;
    int lives[MAX_PLAYERS];
    int current_player_id;
    int last_current_player_id;
    int current_bet_count;
    int current_bet_value;
    bool game_started;
    int my_cards[INITIAL_LIVES];
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
    for (int i = 0; i < INITIAL_LIVES; i++) {
        if (args->my_cards[i] >= 0) {
            printf(GREEN);
            print_card(args->my_cards[i]);
            printf(" " RESET);
        }
    }
    printf("\n");
}

void* receive_thread(void* arg) {
    ClientThreadArgs *args = (ClientThreadArgs*)arg;
    GamePacket pkt;

    args->last_current_player_id = 0;

    while(args->is_running) {
        int res = args->ipc.receive_packet(args->fd, &pkt);

        if (res <= 0) {
            printf(RED "\n[INFO]: 🔌 Spojenie prerušené." RESET "\n");
            args->is_running = 0;
            break;
        }

        switch(pkt.MessageType) {
            case MSG_WELCOME:
                args->player_id = pkt.player_id;
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                printf(CYAN "[SERVER]: 🎮 Vitaj! Si Hráč %d." RESET "\n", pkt.player_id);
                break;

            case MSG_START_ROUND:
                args->game_started = true;

                printf(YELLOW "\n╔════════════════════════════════╗\n");
                printf("║        🎯 NOVÉ KOLO 🎯         ║\n");
                printf("╚════════════════════════════════╝" RESET "\n");

                printf(GREEN "📋 Tvoje karty: " RESET);
                for(int i = 0; i < INITIAL_LIVES; i++) {
                    if (pkt.my_cards[i] >= 0) {
                        printf(GREEN);
                        print_card(pkt.my_cards[i]);
                        printf(" " RESET);
                    }
                }
                printf("\n");

                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                args->current_player_id = pkt.current_player_id;
                args->last_current_player_id = 0;
                memcpy(args->my_cards, pkt.my_cards, sizeof(pkt.my_cards));
                break;

            case MSG_UPDATE: {
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                args->current_player_id = pkt.current_player_id;
                args->current_bet_count = pkt.count;
                args->current_bet_value = pkt.card_value;

                bool waiting = (args->current_player_id == 0 && args->current_bet_count == 0 && !args->game_started);
                if (waiting) {
                    // Zobraz aktualizáciu počtu hráčov
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
                printf(GREEN "%s" RESET "\n\n", pkt.text);
                printf(CYAN "Stlač Enter pre návrat do hlavného menu..." RESET "\n");
                args->is_running = 0;
                break;
        }
    }
    return NULL;
}

void start_new_game(IPC_Interface ipc, ClientThreadArgs *args) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(1);
    }

    if (pid == 0) {
        char *argv[] = {SERVER_PATH, NULL};
        execvp(SERVER_PATH, argv);
        perror("execvp");
        exit(1);
    }

    printf(CYAN "Nová hra sa spúšťa..." RESET "\n");

    sleep(1);

    args->fd = ipc.init_client("127.0.0.1");
    if (args->fd < 0) {
        printf(RED "Nepodarilo sa pripojiť k serveru.\n" RESET);
        exit(1);
    }

    sleep(1);

    GamePacket join_pkt = { .MessageType = MSG_JOIN };
    strcpy(join_pkt.text, "Som hostiteľ");
    ipc.send_packet(args->fd, &join_pkt);
}

int main() {
    IPC_Interface socket_ipc = get_socket_interface();
    ClientThreadArgs args = {0};
    args.ipc = socket_ipc;

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
            case '1': {
                memset(&args, 0, sizeof(args));
                args.ipc = socket_ipc;
                args.is_running = 1;
                args.game_started = false;
                args.last_current_player_id = 0;

                start_new_game(socket_ipc, &args);

                pthread_t recv_tid;
                pthread_create(&recv_tid, NULL, receive_thread, &args);

                char input[256];
                while (args.is_running) {
                    if (!fgets(input, sizeof(input), stdin)) {
                        args.is_running = 0;
                        break;
                    }

                    input[strcspn(input, "\n")] = 0;
                    if (strlen(input) == 0) continue;

                    if (strcmp(input, "quit") == 0) {
                        printf(CYAN "\nNávrat do hlavného menu...\n" RESET);
                        args.is_running = 0;
                        socket_ipc.close_conn(args.fd);
                        break;
                    }

                    GamePacket pkt = {0};
                    if (strcasecmp(input, "klamar") == 0) {
                        pkt.MessageType = MSG_LIAR;
                    } else {
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
                                printf(RED "Zlá karta alebo počet.\n" RESET);
                                continue;
                            }
                        } else {
                            printf(RED "Nerozumiem príkazu.\n" RESET);
                            continue;
                        }
                    }
                    socket_ipc.send_packet(args.fd, &pkt);
                }

                pthread_join(recv_tid, NULL);
                socket_ipc.close_conn(args.fd);
                printf(CYAN "\nNávrat do hlavného menu...\n\n" RESET);
                break;
            }

            case '2': {
                printf(GREEN "Pripájam sa k hre...\n" RESET);

                memset(&args, 0, sizeof(args));
                args.ipc = socket_ipc;
                args.is_running = 1;
                args.game_started = false;
                args.last_current_player_id = 0;

                args.fd = socket_ipc.init_client("127.0.0.1");
                if (args.fd < 0) {
                    printf(RED "Nepodarilo sa pripojiť k serveru.\n" RESET);
                    break;
                }

                GamePacket join_pkt = { .MessageType = MSG_JOIN };
                strcpy(join_pkt.text, "Pripojil som sa");
                socket_ipc.send_packet(args.fd, &join_pkt);

                pthread_t recv_tid;
                pthread_create(&recv_tid, NULL, receive_thread, &args);

                char input[256];
                while (args.is_running) {
                    if (!fgets(input, sizeof(input), stdin)) {
                        args.is_running = 0;
                        break;
                    }

                    input[strcspn(input, "\n")] = 0;
                    if (strlen(input) == 0) continue;

                    if (strcmp(input, "quit") == 0) {
                        printf(CYAN "\nNávrat do hlavného menu...\n" RESET);
                        args.is_running = 0;
                        socket_ipc.close_conn(args.fd);
                        break;
                    }

                    GamePacket pkt = {0};
                    if (strcasecmp(input, "klamar") == 0) {
                        pkt.MessageType = MSG_LIAR;
                    } else {
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
                                printf(RED "Zlá karta alebo počet.\n" RESET);
                                continue;
                            }
                        } else {
                            printf(RED "Nerozumiem príkazu.\n" RESET);
                            continue;
                        }
                    }
                    socket_ipc.send_packet(args.fd, &pkt);
                }

                pthread_join(recv_tid, NULL);
                socket_ipc.close_conn(args.fd);
                printf(CYAN "\nNávrat do hlavného menu...\n\n" RESET);
                break;
            }

            case '3':
                printf(CYAN "Ádiós!\n" RESET);
                return 0;

            case '4':
                printf(BOLD YELLOW "\n╔══════════════════════════════════════════════════╗\n");
                printf("║                PRAVIDLÁ HRY LIAR'S BAR           ║\n");
                printf("╚══════════════════════════════════════════════════╝" RESET "\n");
                printf("- Hra pre " BOLD "2–4 hráčov" RESET " s balíčkom:\n");
                printf("      → 6× Q (kráľovná),\n      → 6× K (kráľ),\n      → 6× A (eso),\n      → 2× J (" BOLD "žolík" RESET ").\n");
                printf("- Každý hráč začína s " BOLD "5 životmi" RESET " (5 kariet v ruke).\n");
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
                break;

            default:
                printf(RED "Neplatná voľba.\n" RESET);
                break;
        }
    }

    return 0;
}