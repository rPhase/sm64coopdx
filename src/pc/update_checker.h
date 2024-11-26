#ifndef _UPDATE_CHECKER_H
#define _UPDATE_CHECKER_H
#include <stdbool.h>
extern bool gUpdateMessage;
#ifndef TARGET_ANDROID

void show_update_popup(void);
void check_for_updates(void);

#endif
#endif