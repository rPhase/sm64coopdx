#ifndef MOD_STORAGE_H
#define MOD_STORAGE_H

#include "mod.h"
#include <stdbool.h>

#define MAX_KEYS 255
#define MAX_KEY_VALUE_LENGTH 64
#define SAVE_DIRECTORY "sav"
#define SAVE_EXTENSION ".sav"

bool mod_storage_save(const char *key, const char *value);
bool mod_storage_save_number(const char* key, f32 value);
bool mod_storage_save_bool(const char* key, bool value);
f32 mod_storage_load_number(const char* key);
bool mod_storage_load_bool(const char* key);
const char *mod_storage_load(const char *key);
#ifdef __ANDROID__
void key_cache_init(void);
#endif

#endif
