#ifndef CLIENT_UI_H
#define CLIENT_UI_H

#include "client_state.h"

#define RESET "\x1b[0m"
#define CLEAR_SCREEN "\033[H\033[J"

void ui_show_welcome();
void ui_show_rules();
void ui_render_game(ClientState* state);

int ui_get_int(const char* prompt, int min, int max, int default_value);
void ui_wait_enter();

#endif
