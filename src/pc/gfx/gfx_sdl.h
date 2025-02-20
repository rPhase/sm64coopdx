#ifndef GFX_SDL_H
#define GFX_SDL_H

#include "gfx_window_manager_api.h"

extern struct GfxWindowManagerAPI gfx_sdl;

#ifdef TOUCH_CONTROLS
void gfx_sdl_set_touchscreen_callbacks(void (*down)(void* event), void (*motion)(void* event), void (*up)(void* event));

#endif
#endif
