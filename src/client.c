#include "../include/common.h"
#include "../include/ipc_interface.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>

#define SERVER_PATH "./server"  // cesta k spustiteľnému serveru (v build priečinku)

typedef struct ClientThreadArgs {
    int fd;
    IPC_Interface ipc;
    volatile int is_running;
    int player_id;
    int my_lives;
    int lives[MAX_PLAYERS];
    int current_player_id;
    int current_bet_count;
    int current_bet_value;
} ClientThreadArgs;

void print_card(int value) {
    switch(value) {
        case 0: printf("Q"); break;
        case 1: printf("K"); break;
        case 2: printf("A"); break;
        case 3: printf("J"); break;
        default: printf("?"); break;
    }
}

void* receive_thread(void* arg) {
    ClientThreadArgs *args = (ClientThreadArgs*)arg;
    GamePacket pkt;

    while(args->is_running) {
        int res = args->ipc.receive_packet(args->fd, &pkt);
        
        if (res <= 0) {
            printf("\n[INFO]: Spojenie so serverom bolo prerušené.\n");
            args->is_running = 0;
            break;
        }

        switch(pkt.MessageType) {
            case MSG_WELCOME:
                args->player_id = pkt.player_id;
                printf("\n[SERVER]: %s\n", pkt.text);
                break;

            case MSG_START_ROUND:
                printf("\n=== NOVÉ KOLO ===\n");
                printf("Tvoje karty: ");
                for(int i = 0; i < INITIAL_LIVES; i++) {
                    if (pkt.my_cards[i] >= 0) {
                        print_card(pkt.my_cards[i]);
                        printf(" ");
                    }
                }
                printf("\n");
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                args->current_player_id = pkt.current_player_id;
                break;

            case MSG_UPDATE:
                args->current_player_id = pkt.current_player_id;
                args->current_bet_count = pkt.count;
                args->current_bet_value = pkt.card_value;
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                printf("\n[UPDATE]: %s\n", pkt.text);
                break;

            case MSG_GAME_OVER:
                printf("\n=== KONIEC HRY ===\n%s\n", pkt.text);
                args->is_running = 0;
                break;

            default:
                printf("\n[SERVER]: %s\n", pkt.text);
        }

        // Zobrazenie aktuálneho stavu
        printf("\nŽivoty: ");
        for(int i = 0; i < MAX_PLAYERS; i++) {
            if (args->lives[i] > 0 || i + 1 == args->player_id) {
                printf("Hráč %d: %d  ", i+1, args->lives[i]);
            }
        }
        printf("\n");

        if (args->current_bet_count > 0) {
            printf("Aktuálna stávka: %dx ", args->current_bet_count);
            print_card(args->current_bet_value);
            printf("\n");
        }

        printf("Na ťahu je hráč %d", args->current_player_id);
        if (args->current_player_id == args->player_id) {
            printf(" <-- TY!");
        }
        printf("\n> ");
        fflush(stdout);
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
        // Child proces - spusti server
        char *argv[] = {SERVER_PATH, NULL};
        execvp(SERVER_PATH, argv);
        perror("execvp server");
        exit(1);
    }

    // Parent (klient) - počká chvíľu a pripojí sa
    sleep(1);  // aby sa server stihol naštartovať

    args->fd = ipc.init_client("127.0.0.1");
    if (args->fd < 0) {
        printf("Nepodarilo sa pripojiť k novovytvorenému serveru.\n");
        exit(1);
    }

    printf("Server spustený a pripojený ako prvý hráč.\n");
}

int main() {
    IPC_Interface socket_ipc = get_socket_interface();
    ClientThreadArgs args = {0};
    args.ipc = socket_ipc;
    args.is_running = 1;

    while(1) {
        printf("\n=== LIAR'S BAR ===\n");
        printf("1. Nová hra\n");
        printf("2. Pripojenie k hre\n");
        printf("3. Koniec\n");
        printf("Voľba: ");
        fflush(stdout);

        char line[10];
        if (!fgets(line, sizeof(line), stdin)) break;

        int choice = atoi(line);

        if (choice == 1) {
            start_new_game(socket_ipc, &args);

            // Pošli JOIN
            GamePacket join_pkt = { .MessageType = MSG_JOIN };
            strcpy(join_pkt.text, "Som hostiteľ");
            socket_ipc.send_packet(args.fd, &join_pkt);

            break;  // prejdi do herného loopu
        }
        else if (choice == 2) {
            // Zatiaľ fixný localhost
            args.fd = socket_ipc.init_client("127.0.0.1");
            if (args.fd < 0) {
                printf("Nepodarilo sa pripojiť. Skús neskôr alebo spusti novú hru.\n");
                continue;
            }

            GamePacket join_pkt = { .MessageType = MSG_JOIN };
            strcpy(join_pkt.text, "Pripojil som sa");
            socket_ipc.send_packet(args.fd, &join_pkt);

            break;
        }
        else if (choice == 3) {
            printf("Dovidenia!\n");
            return 0;
        }
        else {
            printf("Neplatná voľba.\n");
        }
    }

    // Spusti receive vlákno
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_thread, &args);
    
    // Hlavný thread na vstup od hráča
    char input[256];
    while(args.is_running && fgets(input, sizeof(input), stdin)) {
        input[strcspn(input, "\n")] = 0;  // odstráň \n

        if (strlen(input) == 0) continue;

        GamePacket pkt = {0};

        if (strcmp(input, "klamar") == 0 || strcmp(input, "liar") == 0) {
            pkt.MessageType = MSG_LIAR;
            strcpy(pkt.text, "KLAMÁR!");
        } else {
            // Očakávame formát napr. "3 K" alebo "5 A"
            int count;
            char card_char;
            if (sscanf(input, "%d %c", &count, &card_char) == 2) {
                int value = -1;
                switch(toupper(card_char)) {
                    case 'Q': value = 0; break;
                    case 'K': value = 1; break;
                    case 'A': value = 2; break;
                    case 'J': value = 3; break;
                }
                if (value != -1 && count > 0) {
                    pkt.MessageType = MSG_BET;
                    pkt.count = count;
                    pkt.card_value = value;
                } else {
                    printf("Neznáma karta. Použi Q, K, A alebo J.\n> ");
                    continue;
                }
            } else {
                printf("Nesprávny formát. Použi napr. '3 K' alebo 'klamar'.\n> ");
                continue;
            }
        }

        socket_ipc.send_packet(args.fd, &pkt);
    }

    args.is_running = 0;
    pthread_join(recv_thread, NULL);
    socket_ipc.close_conn(args.fd);

    return 0;
}