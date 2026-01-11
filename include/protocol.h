#ifndef PROTOCOL_H
#define PROTOCOL_H

// Len sieťový protokol - bez sys/, pthread, UI fariek
typedef enum MessageType {
    MSG_JOIN, MSG_WELCOME, MSG_START_ROUND, MSG_BET, 
    MSG_LIAR, MSG_QUIT, MSG_UPDATE, MSG_GAME_OVER
} MessageType;

typedef enum CardValue {
    CARD_QUEEN = 0, CARD_KING = 1, CARD_ACE = 2, CARD_JOKER = 3
} CardValue;

typedef struct GamePacket {
    MessageType MessageType;
    int player_id;
    int card_value;
    int count;
    int game_id;
    int my_cards[5];
    int lives[4];
    int current_player_id;
    char text[256];
} GamePacket;

#define PORT 9999
#define MAX_PLAYERS 4
#define MIN_PLAYERS 2
#define MAX_GAMES 10
#define MAX_LIVES 5

#endif