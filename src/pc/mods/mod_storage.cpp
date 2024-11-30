#include "mod_storage.h"

#include <iostream>
#include <fstream>
#include <string>
#include "pc/mini.h"

extern "C" {
#include "pc/platform.h"
#include "pc/mods/mod.h"
#include "pc/lua/smlua.h"
#include "pc/mods/mods_utils.h"
#include "pc/fs/fs.h"
#include "pc/debuglog.h"
}

#define path_exists fs_sys_path_exists

#ifdef __ANDROID__
#include "pc/utils/misc.h"

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

bool Char_Valid(char* buffer) {
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

void Mod_Storage_Get_Filename(char* dest) {
    const char *path = sys_user_path(); // get base sm64ex-coop appdata dir
    snprintf(dest, SYS_MAX_PATH - 1, "%s/sav/%s", path, gLuaActiveMod->relativePath); // append sav folder
    strdelete(dest, (char *)".lua"); // delete ".lua" from sav name
    strcat(dest, SAVE_EXTENSION); // append SAVE_EXTENSION
    normalize_path(dest); // fix any out of place slashes
}

extern "C" {

bool mod_storage_save(const char *key, const char *value) {
    if (strlen(key) > MAX_KEY_VALUE_LENGTH || strlen(value) > MAX_KEY_VALUE_LENGTH) {
        return false;
    }
    if (!Char_Valid((char *)key) || !Char_Valid((char *)value)) {
        return false;
    }

#ifdef __ANDROID__
    if (!key_cached(key, value)) {
        cache_key(key, value);
    }
#endif

    char filename[SYS_MAX_PATH] = {0};
    Mod_Storage_Get_Filename(filename);

    // ensure savPath exists
    char savPath[SYS_MAX_PATH] = { 0 };
    if (snprintf(savPath, SYS_MAX_PATH - 1, "%s", fs_get_write_path(SAVE_DIRECTORY)) < 0) {
        return false;
    }
    if (!fs_sys_dir_exists(savPath)) { fs_sys_mkdir(savPath); }

    mINI::INIFile file(filename);
    mINI::INIStructure ini;
    file.read(ini);

    if (ini["storage"].size() + 1 > MAX_KEYS) {
        return false;
    }

    ini["storage"][key] = value;

    file.write(ini);

    file.generate(ini);

    return true;
}

bool mod_storage_save_number(const char* key, f32 value) {
    return mod_storage_save(key, std::to_string(value).c_str());
}

bool mod_storage_save_bool(const char* key, bool value) {
    return mod_storage_save(key, value ? "true" : "false");
}

const char *mod_storage_load(const char *key) {
    if (strlen(key) > MAX_KEY_VALUE_LENGTH) {
        return NULL;
    }
    if (!Char_Valid((char *)key)) {
        return NULL;
    }
    
#ifdef __ANDROID__
    char *cached_value = NULL;
    cached_value = key_cached(key, NULL);
    if (cached_value) {
        return cached_value;
    }
#endif

    char filename[SYS_MAX_PATH] = {0};
    Mod_Storage_Get_Filename(filename);

    if (!path_exists(filename)) {
        return NULL;
    }

    mINI::INIFile file(filename);
    mINI::INIStructure ini;
    file.read(ini);

    return const_cast<char*>(ini["storage"][key].c_str());

#ifdef __ANDROID__
    cache_key(key, value);
#endif

}

f32 mod_storage_load_number(const char *key) {
    const char *value = mod_storage_load(key);
    if (value == NULL) { return 0; }

    return std::strtod(value, nullptr);
}

bool mod_storage_load_bool(const char *key) {
    const char *value = mod_storage_load(key);
    if (value == NULL) { return false; }

    return !strcmp(value, "true");
}

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