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

#define SERVER_PATH "server"

typedef struct ClientThreadArgs {
    int fd;
    IPC_Interface ipc;
    volatile int is_running;
    int player_id;
    int lives[MAX_PLAYERS];
    int current_player_id;
    int current_bet_count;
    int current_bet_value;
    bool game_started;
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

void* receive_thread(void* arg) {
    ClientThreadArgs *args = (ClientThreadArgs*)arg;
    GamePacket pkt;

    while(args->is_running) {
        int res = args->ipc.receive_packet(args->fd, &pkt);

        if (res <= 0) {
            printf("\n[INFO]: Spojenie prerušené.\n");
            args->is_running = 0;
            break;
        }

        switch(pkt.MessageType) {
            case MSG_WELCOME:
                args->player_id = pkt.player_id;
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                printf("\n[SERVER]: %s\n", pkt.text);
                break;

            case MSG_START_ROUND:
                args->game_started = true;
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
                memcpy(args->lives, pkt.lives, sizeof(pkt.lives));
                args->current_player_id = pkt.current_player_id;
                args->current_bet_count = pkt.count;
                args->current_bet_value = pkt.card_value;
                printf("\n[UPDATE]: %s\n", pkt.text);
                break;

            case MSG_GAME_OVER:
                printf("\n=== KONIEC HRY ===\n%s\n", pkt.text);
                args->is_running = 0;
                break;
        }

        if (args->game_started) {
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
            printf("\n");

            printf("> ");
            fflush(stdout);
        } else {
            printf("> ");
            fflush(stdout);
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

    sleep(1);

    args->fd = ipc.init_client("127.0.0.1");
    if (args->fd < 0) {
        printf("Nepodarilo sa pripojiť.\n");
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
        printf("\n=== LIAR'S BAR ===\n");
        printf("1. Nová hra\n");
        printf("2. Pripojenie k hre\n");
        printf("3. Koniec\n");
        printf("4. Pravidlá hry\n");
        printf("Voľba: ");
        fflush(stdout);

        char line[10];
        if (!fgets(line, sizeof(line), stdin)) break;

        int choice = atoi(line);

        if (choice == 1) {
            args.is_running = 1;
            args.game_started = false;
            start_new_game(socket_ipc, &args);
        }
        else if (choice == 2) {
            args.is_running = 1;
            args.game_started = false;
            args.fd = socket_ipc.init_client("127.0.0.1");
            if (args.fd < 0) {
                printf("Nepodarilo sa pripojiť.\n");
                continue;
            }

            sleep(1);

            GamePacket join_pkt = { .MessageType = MSG_JOIN };
            strcpy(join_pkt.text, "Pripojil som sa");
            socket_ipc.send_packet(args.fd, &join_pkt);
        }
        else if (choice == 3) {
            return 0;
        }
        else if (choice == 4) {
            printf("\n=== PRAVIDLÁ HRY LIAR'S BAR ===\n");
            printf(" - Hra pre 2-4 hráčov s balíčkom 6x Q, 6x K, 6x A, 2x J (žolík).\n");
            printf(" - Každý hráč začína s 5 životmi (kartami).\n");
            printf(" - Hráči sa striedajú v stávkach na celkový počet a hodnotu kariet na stole (napr. '3 K' = aspoň 3 krále).\n");
            printf(" - Stávka musí byť vyššia (väčší počet alebo rovnaký počet a vyššia hodnota: Q < K < A < J).\n");
            printf(" - Hráč na ťahu môže stávkovať alebo volať 'klamar' na predchádzajúcu stávku.\n");
            printf(" - Ak je klamár úspešný, stávkujúci stráca život. Ak nie, klamár stráca život.\n");
            printf(" - Po strate života sa rozdajú nové karty (podľa aktuálnych životov).\n");
            printf(" - Hra končí, keď ostane jeden hráč s životmi.\n");
            printf(" - Príkaz 'quit' kedykoľvek pre návrat do menu.\n\n");
            continue;
        }
        else {
            printf("Neplatná voľba.\n");
            continue;
        }

        pthread_t recv_thread;
        pthread_create(&recv_thread, NULL, receive_thread, &args);

        char input[256];
        while(args.is_running && fgets(input, sizeof(input), stdin)) {
            input[strcspn(input, "\n")] = 0;

            if (strlen(input) == 0) continue;

            if (strcmp(input, "quit") == 0) {
                printf("\nNávrat do hlavného menu...\n");
                args.is_running = 0;
                socket_ipc.close_conn(args.fd);
                pthread_join(recv_thread, NULL);
                args.game_started = false;
                args.current_player_id = 0;
                args.current_bet_count = 0;
                args.current_bet_value = -1;
                args.player_id = 0;
                memset(args.lives, 0, sizeof(args.lives));
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
                        printf("Zlá karta alebo počet.\n> ");
                        continue;
                    }
                } else {
                    printf("Nerozumiem príkazu.\n> ");
                    continue;
                }
            }

            socket_ipc.send_packet(args.fd, &pkt);
        }

        pthread_join(recv_thread, NULL);
        socket_ipc.close_conn(args.fd);
    }

    return 0;
}