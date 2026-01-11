#ifndef CLIENT_UI_H
#define CLIENT_UI_H

#include "client_state.h"

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define WHITE "\x1b[37m"
#define BOLD "\x1b[1m"
#define RESET "\x1b[0m"
#define CLEAR_SCREEN "\033[H\033[J"

void ui_print_welcome();
void ui_print_rules();
void ui_render_game(ClientState* state);

int ui_get_int(const char* prompt, int min, int max, int def_val);
void ui_wait_enter();

#endif
