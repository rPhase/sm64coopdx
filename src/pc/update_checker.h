#ifndef _UPDATE_CHECKER_H
#define _UPDATE_CHECKER_H
#ifndef __ANDROID__

#include <stdbool.h>

extern bool gUpdateMessage;

void show_update_popup(void);
void check_for_updates(void);

#else
#include <stdbool.h>

extern bool gUpdateMessage;
#endif
#endif