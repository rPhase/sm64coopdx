#ifndef _UPDATE_CHECKER_H
#define _UPDATE_CHECKER_H
extern bool gUpdateMessage
#ifndef TARGET_ANDROID
#include <stdbool.h>

void show_update_popup(void);
void check_for_updates(void);

#endif
#endif