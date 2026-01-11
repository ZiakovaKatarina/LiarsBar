#ifndef COMMON_H
#define COMMON_H

#define PORT 9999
#define MAX_PLAYERS 4
#define MIN_PLAYERS 2
#define MAX_GAMES 10
#define MAX_LIVES 5

typedef enum {
    MSG_JOIN,
    MSG_WELCOME,
    MSG_START_ROUND,
    MSG_BET,
    MSG_LIAR,
    MSG_QUIT,
    MSG_UPDATE,
    MSG_GAME_OVER
} MessageType;

typedef enum {
    CARD_QUEEN = 0,
    CARD_KING = 1,
    CARD_ACE = 2,
    CARD_JOKER = 3
} CardValue;

typedef struct GamePacket {
    MessageType type;
    int game_id;
    int player_id;

    int count;
    int card_value;

    int my_cards[MAX_LIVES];
    int lives[MAX_PLAYERS];
    int current_player_id;

    char text[256];
} GamePacket;

#endif