// include/logic.h
#ifndef LOGIC_H
#define LOGIC_H

#include "common.h"
#include "ipc_interface.h"

void rozdaj_karty_vsetkym(int player_cards[MAX_PLAYERS][INITIAL_LIVES],
                          int sockets[MAX_PLAYERS],
                          IPC_Interface ipc,
                          int lives[MAX_PLAYERS],
                          int current_player);

#endif