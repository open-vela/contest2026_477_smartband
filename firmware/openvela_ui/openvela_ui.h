#ifndef OPENVELA_UI_H
#define OPENVELA_UI_H
#include <stddef.h>

void openvela_ui_create(void);
/* UI-thread-only inspection for the local development transport. */
int openvela_ui_status(char *buffer, size_t capacity);

#endif
