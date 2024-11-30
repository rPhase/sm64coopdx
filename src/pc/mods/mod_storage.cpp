#include "mod_storage.h"

#include <stdio.h>
#include "pc/mini.h"
extern "C" {
#include "pc/platform.h"
#include "pc/configini.h" // for writing
#include "pc/ini.h" // for parsing
#include "pc/lua/smlua.h"
#include "pc/mods/mods_utils.h"
#include "pc/debuglog.h"
#include <stdbool.h>
#include "pc/fs/fs.h"
#include "pc/utils/misc.h"
#include <ctype.h>
}
#ifdef __ANDROID__

#define MAX_CACHED_KEYS 100
typedef struct CachedKey {
    char key[MAX_KEY_VALUE_LENGTH],
         value[MAX_KEY_VALUE_LENGTH];
} CachedKey;

static CachedKey sCachedKeys[MAX_CACHED_KEYS];

extern "C" {
void key_cache_init(void) {
    for (u32 i = 0; i < MAX_CACHED_KEYS; i++) {
        snprintf(sCachedKeys[i].key, MAX_KEY_VALUE_LENGTH, "%s", "");
        snprintf(sCachedKeys[i].value, MAX_KEY_VALUE_LENGTH, "%s", "");
    }
}
}

char *key_cached(const char *key, const char * value) {
    for (u32 i = 0; i < MAX_CACHED_KEYS; i++) {
        if (strncmp(key, sCachedKeys[i].key, MAX_KEY_VALUE_LENGTH) == 0) {
            if (value) {
                snprintf(sCachedKeys[i].value, MAX_KEY_VALUE_LENGTH, "%s", value);
            }
            return sCachedKeys[i].value;
        }
    }
    return NULL;
}

void cache_key(const char * key, const char * value) {
    for (u32 i = 0; i < MAX_CACHED_KEYS; i++) {
        if (strncmp("", sCachedKeys[i].key, MAX_KEY_VALUE_LENGTH) == 0) {
            snprintf(sCachedKeys[i].key, MAX_KEY_VALUE_LENGTH, "%s", key);
            snprintf(sCachedKeys[i].value, MAX_KEY_VALUE_LENGTH, "%s", value);
            return;
        }
    }
}
#endif

void strdelete(char string[], char substr[]) {
    // i is used to loop through the string
    u16 i = 0;

    // store the lengths of the string and substr
    u16 string_length = strlen(string);
    u16 substr_length = strlen(substr);

    // loop through starting at the first index
    while (i < string_length) {
        // if we find the substr at the current index, delete it
        if (strstr(&string[i], substr) == &string[i]) {
            // determine the string's new length after removing the substr occurrence
            string_length -= substr_length;
            // shift forward the remaining characters in the string after the substr 
            // occurrence by the length of substr, effectively removing it!
            for (u16 j = i; j < string_length; j++) {
                string[j] = string[j + substr_length];
            }
        } else {
            i++;
        }
    }

    string[i] = '\0';
}

bool char_valid(char* buffer) {
    if (buffer[0] == '\0') { return false; }
    while (*buffer != '\0') {
        if ((*buffer >= 'a' && *buffer <= 'z') || (*buffer >= 'A' && *buffer <= 'Z') || (*buffer >= '0' && *buffer <= '9') || *buffer == '_' || *buffer == '.' || *buffer == '-') {
            buffer++;
            continue;
        }
        return false;
    }
    return true;
}

s32 key_count(char* filename) {
    FILE *file;
    file = fopen(filename, "r");
    if (file == NULL) { return 0; }

#ifdef __ANDROID__
    // something is wrong with the original
    // implementation of this function (seen below
    // in the else block) on Android, so copy the
    // logic used in mod.c which seems to work
    // better
    s32 lines = 0;
    char buffer[512] = { 0 };
    while (!feof(file)) {
        file_get_line(buffer, 512, file);
        lines++;
    }
#else
    s32 lines = 1;
    char c;
    do {
        c = fgetc(file);
        if (c == '\n') { lines++; }
    } while (c != EOF);
#endif

    fclose(file);
    
    return lines - 4;
}

void mod_storage_get_filename(char* dest) {
    const char *path = sys_user_path(); // get base sm64ex-coop appdata dir
    snprintf(dest, SYS_MAX_PATH - 1, "%s/sav/%s", path, gLuaActiveMod->relativePath); // append sav folder
    strdelete(dest, ".lua"); // delete ".lua" from sav name
    strcat(dest, SAVE_EXTENSION); // append SAVE_EXTENSION
    normalize_path(dest); // fix any out of place slashes
}
extern "C" {
bool mod_storage_save(const char *key, const char *value) {
    if (strlen(key) > MAX_KEY_VALUE_LENGTH || strlen(value) > MAX_KEY_VALUE_LENGTH) {
        LOG_LUA_LINE("Too long of a key and or value for mod_storage_save()");
        return false;
    }
    if (!char_valid((char *)key) || !char_valid((char *)value)) {
        LOG_LUA_LINE("Invalid key and or value passed to mod_storage_save()");
        return false;
    }

#ifdef __ANDROID__
    if (!key_cached(key, value)) {
        cache_key(key, value);
    }
#endif

    FILE *file;
    Config *cfg = NULL;
    char *filename;
    filename = (char *)malloc((SYS_MAX_PATH - 1) * sizeof(char));
    mod_storage_get_filename(filename);

    // ensure savPath exists
    char savPath[SYS_MAX_PATH] = { 0 };
    if (snprintf(savPath, SYS_MAX_PATH - 1, "%s", fs_get_write_path(SAVE_DIRECTORY)) < 0) {
        LOG_ERROR("Failed to concat sav path");
        free(filename);
        return false;
    }
    if (!fs_sys_dir_exists(savPath)) { fs_sys_mkdir(savPath); }

    bool exists = fs_sys_path_exists(filename);
    file = fopen(filename, exists ? "r+" : "w");
    cfg = ConfigNew();
    if (exists) {
        if (ConfigReadFile(filename, &cfg) != CONFIG_OK) {
            ConfigFree(cfg);
            fclose(file);
            free(filename);
            return false;
        }
        if (key_count(filename) > MAX_KEYS) {
            LOG_LUA_LINE("Tried to save more than MAX_KEYS with mod_storage_save()");
            ConfigFree(cfg);
            fclose(file);
            free(filename);
            return false;
        }
    }

    char lowerKey[MAX_KEY_VALUE_LENGTH];
    snprintf(lowerKey, MAX_KEY_VALUE_LENGTH, "%s", key);
    for (int i = 0; i < MAX_KEY_VALUE_LENGTH; i++) {
        if (lowerKey[i] == '\0') { break; }
        lowerKey[i] = tolower(lowerKey[i]);
    }

    ConfigRemoveKey(cfg, "storage", lowerKey);
    ConfigRemoveKey(cfg, "storage", key);
    ConfigAddString(cfg, "storage", key, value);

    fclose(file);
    ConfigPrintToFile(cfg, filename);
    ConfigFree(cfg);
    free(filename);

    return true;
}

bool mod_storage_save_number(const char* key, f32 value) {
    // Store string results in a temporary buffer
    // this assumes mod_storage_load will only ever be called by Lua
    static char str[MAX_KEY_VALUE_LENGTH];
    if (floor(value) == value) {
        snprintf(str, MAX_KEY_VALUE_LENGTH, "%lld", (s64)value);
    } else {
        snprintf(str, MAX_KEY_VALUE_LENGTH, "%f", value);
    }

    return mod_storage_save(key, str);
}

bool mod_storage_save_bool(const char* key, bool value) {
    return mod_storage_save(key, value ? "true" : "false");
}

const char *mod_storage_load(const char *key) {
    if (strlen(key) > MAX_KEY_VALUE_LENGTH) {
        LOG_LUA_LINE("Too long of a key for mod_storage_load()");
        return NULL;
    }
    if (!char_valid((char *)key)) {
        LOG_LUA_LINE("Invalid key passed to mod_storage_save()");
        return NULL;
    }

#ifdef __ANDROID__
    char *cached_value = NULL;
    cached_value = key_cached(key, NULL);
    if (cached_value) {
        return cached_value;
    }
#endif

    char *filename;
    filename = (char *)malloc((SYS_MAX_PATH - 1) * sizeof(char));
    mod_storage_get_filename(filename);
    static char value[MAX_KEY_VALUE_LENGTH];
    ini_t *storage;

    if (!fs_sys_path_exists(filename)) {
        free(filename);
        return NULL;
    }

    storage = ini_load(filename);
    if (storage == NULL) {
        ini_free(storage);
        free(filename);
        return NULL;
    }
    snprintf(value, MAX_KEY_VALUE_LENGTH, "%s", ini_get(storage, "storage", key));

    ini_free(storage);
    free(filename);

    if (strstr(value, "(null)") != NULL) { return NULL; }

#ifdef __ANDROID__
    cache_key(key, value);
#endif

    return value;
}

f32 mod_storage_load_number(const char* key) {
    const char* value = mod_storage_load(key);
    if (value == NULL) { return 0; }

    return strtof(value, NULL);
}

bool mod_storage_load_bool(const char* key) {
    const char* value = mod_storage_load(key);
    if (value == NULL) { return false; }

    return !strcmp(value, "true");
}

#define Mod_Storage_Get_Filename mod_storage_get_filename
#define Char_Valid char_valid
#define path_exists fs_sys_path_exists

bool mod_storage_remove(const char* key) {
    if (gLuaActiveMod == NULL) { return false; }
    if (strlen(key) > MAX_KEY_VALUE_LENGTH) { return false; }
    if (!Char_Valid((char *)key)) { return false; }

    char filename[SYS_MAX_PATH] = { 0 };
    Mod_Storage_Get_Filename(filename);
    if (!fs_sys_path_exists(filename)) { return false; }

    mINI::INIFile file(filename);
    mINI::INIStructure ini;
    file.read(ini);

    if (ini["storage"].remove(key)) {
        file.write(ini);
        file.generate(ini);
        return true;
    }

    return false;
}

bool mod_storage_clear(void) {
    char filename[SYS_MAX_PATH] = {0};
    Mod_Storage_Get_Filename(filename);

    if (!path_exists(filename)) {
        return false;
    }

    mINI::INIFile file(filename);
    mINI::INIStructure ini;
    file.read(ini);

    if (ini["storage"].size() == 0) {
        return false;
    }

    ini["storage"].clear();

    file.write(ini);

    file.generate(ini);

    return true;
}
}