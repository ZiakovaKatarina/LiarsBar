#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define MAX_PLAYERS 4
#define MIN_PLAYERS 2
#define INITIAL_LIVES 2
#define MSG_TEST 99

#define CARD_QUEEN 0
#define CARD_KING 1
#define CARD_ACE 2
#define CARD_JOKER 3

typedef enum MessageType {
    MSG_JOIN,
    MSG_WELCOME,
    MSG_START_ROUND,
    MSG_BET,
    MSG_LIAR,
    MSG_UPDATE,
    MSG_GAME_OVER
} MessageType;


typedef enum IPCType {
    IPC_SOCKETS,
    IPC_SHARED_MEMORY,
    IPC_PIPES
} IPCType;

typedef struct GamePacket {
    MessageType MessageType;
    int player_id;
    int card_value;
    int count;
    int my_cards[INITIAL_LIVES];
    int lives[MAX_PLAYERS];
    int current_player_id;
    char text[256];
} GamePacket;

#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define WHITE   "\x1b[37m"
#define BOLD    "\x1b[1m"
#define RESET   "\x1b[0m"

#endif