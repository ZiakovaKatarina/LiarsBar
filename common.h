typedef enum MessageType {
    JOIN,
    BET,
    LIAR,
    UPDATE,
    END
} MessageType;

typedef struct GamePacket {
    MessageType MessageType;
    int player_id;
    int card_value;
    int count;
    int lives[4];
} GamePacket;