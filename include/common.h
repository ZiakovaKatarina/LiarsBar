#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9999
#define MAX_PLAYERS 4
#define MIN_PLAYERS 2
#define MAX_GAMES 10
#define MAX_LIVES 5

typedef enum MessageType {
    MSG_JOIN,
    MSG_WELCOME,
    MSG_START_ROUND,
    MSG_BET,
    MSG_LIAR,
    MSG_QUIT,
    MSG_UPDATE,
    MSG_GAME_OVER
} MessageType;

typedef enum CardValue {
    CARD_QUEEN = 0,
    CARD_KING = 1,
    CARD_ACE = 2,
    CARD_JOKER = 3
} CardValue;

typedef struct GamePacket {
    MessageType MessageType;
    int player_id;
    int card_value;
    int count;
    int game_id;
    int my_cards[MAX_LIVES];
    int lives[MAX_PLAYERS];
    int current_player_id;
    char text[256];
} GamePacket;

#include "ipc_interface.h"

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