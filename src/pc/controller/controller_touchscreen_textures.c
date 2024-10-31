#ifdef TOUCH_CONTROLS
#include "controller_touchscreen_textures.h"

ALIGNED8 const u8 texture_touch_button[] = {
#include "textures/touchcontrols/touch_button.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_button_dark[] = {
#include "textures/touchcontrols/touch_button_dark.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_cup[] = {
#include "textures/touchcontrols/touch_cup.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_cdown[] = {
#include "textures/touchcontrols/touch_cdown.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_cleft[] = {
#include "textures/touchcontrols/touch_cleft.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_cright[] = {
#include "textures/touchcontrols/touch_cright.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_chat[] = {
#include "textures/touchcontrols/touch_chat.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_up[] = {
#include "textures/touchcontrols/touch_up.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_down[] = {
#include "textures/touchcontrols/touch_down.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_left[] = {
#include "textures/touchcontrols/touch_left.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_right[] = {
#include "textures/touchcontrols/touch_right.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_lua[] = {
#include "textures/touchcontrols/touch_lua.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_check[] = {
#include "textures/touchcontrols/touch_check.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_cross[] = {
#include "textures/touchcontrols/touch_cross.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_reset[] = {
#include "textures/touchcontrols/touch_reset.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_snap[] = {
#include "textures/touchcontrols/touch_snap.rgba16.inc.c"
};
ALIGNED8 const u8 texture_touch_trash[] = {
#include "textures/touchcontrols/touch_trash.rgba16.inc.c"
};

const Texture *const touch_textures[TOUCH_TEXTURE_COUNT] = {
    texture_touch_button,
    texture_touch_button_dark,
    texture_touch_cup,
    texture_touch_cdown,
    texture_touch_cleft,
    texture_touch_cright,
    texture_touch_chat,
    texture_touch_up,
    texture_touch_down,
    texture_touch_left,
    texture_touch_right,
    texture_touch_lua,
    texture_touch_check,
    texture_touch_cross,
    texture_touch_reset,
    texture_touch_snap,
    texture_touch_trash,
};
#endif