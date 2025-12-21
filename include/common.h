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
#define INITIAL_LIVES 5

#define CARD_QUEEN 12
#define CARD_KING 13
#define CARD_ACE 14
#define CARD_JOKER 15

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

#endif