#include <stdio.h>
#include "version.h"
#include "types.h"
#include "pc/configfile.h"

static char sVersionString[MAX_VERSION_LENGTH] = { 0 };
static char sOnlineVersionString[MAX_VERSION_LENGTH] = { 0 };

#ifdef PLATFORM_COMPAT
char *gameVersionChoices[MAX_PLATFORM_VERSION] = {
    "PC",
    "Android",
    "All"
};
#endif

const char* get_version(void) {
#if defined(VERSION_US)
    snprintf(sVersionString, MAX_VERSION_LENGTH, "%s", SM64COOPDX_VERSION);
#else
    snprintf(sVersionString, MAX_VERSION_LENGTH, "%s %s", SM64COOPDX_VERSION, VERSION_REGION);
#endif
    return sVersionString;
}

const char* get_version_online(void) {
#if defined(VERSION_US)
    if (MINOR_VERSION_NUMBER > 0) {
        snprintf(sOnlineVersionString, MAX_VERSION_LENGTH, "%s%d.%d", VERSION_TEXT, VERSION_NUMBER, MINOR_VERSION_NUMBER);
    } else {
        snprintf(sOnlineVersionString, MAX_VERSION_LENGTH, "%s%d", VERSION_TEXT, VERSION_NUMBER);
    }
#else
    if (MINOR_VERSION_NUMBER > 0) {
        snprintf(sOnlineVersionString, MAX_VERSION_LENGTH, "%s%d.%d %s", VERSION_TEXT, VERSION_NUMBER, MINOR_VERSION_NUMBER, VERSION_REGION);
    } else {
        snprintf(sOnlineVersionString, MAX_VERSION_LENGTH, "%s%d %s", VERSION_TEXT, VERSION_NUMBER, VERSION_REGION);
    }
#endif
    return sOnlineVersionString;
}
