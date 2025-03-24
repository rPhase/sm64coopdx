#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_controls_n64.h"
#include "djui_panel_controls_extra.h"
#include "pc/utils/misc.h"
#include "pc/configfile.h"
#include "pc/controller/controller_api.h"
#include "pc/controller/controller_sdl.h"
#ifdef TOUCH_CONTROLS
#include "src/pc/controller/controller_touchscreen.h"
#endif

void djui_panel_controls_value_change(UNUSED struct DjuiBase* caller) {
    controller_reconfigure();
}

void djui_panel_controls_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(CONTROLS, CONTROLS), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
    #ifdef TOUCH_CONTROLS // TODO: Get translations for touch controls options
        struct DjuiButton* n64bindsButton = djui_button_create(body, DLANG(CONTROLS, N64_BINDS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_controls_n64_create);
        djui_base_set_size(&n64bindsButton->base, 1.0f, 43);
        struct DjuiButton* extrabindsButton = djui_button_create(body, DLANG(CONTROLS, EXTRA_BINDS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_controls_extra_create);
        djui_base_set_size(&extrabindsButton->base, 1.0f, 43);
        struct DjuiButton* touchbindsButton = djui_button_create(body, "Touch Binds", DJUI_BUTTON_STYLE_NORMAL, djui_panel_shutdown_touchconfig);
        djui_base_set_size(&touchbindsButton->base, 1.0f, 43);
        djui_checkbox_create(body, "Autohide Touch Controls", &configAutohideTouch, NULL);
        djui_checkbox_create(body, "Slide Touch", &configSlideTouch, NULL);
        djui_checkbox_create(body, "Ignore Phantom Touch Events", &configPhantomTouch, NULL);
    #else
        djui_button_create(body, DLANG(CONTROLS, N64_BINDS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_controls_n64_create);
        djui_button_create(body, DLANG(CONTROLS, EXTRA_BINDS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_controls_extra_create);
        djui_checkbox_create(body, DLANG(CONTROLS, BACKGROUND_GAMEPAD), &configBackgroundGamepad, NULL);
    #endif
#if defined(HANDHELD) && !defined(__ANDROID__)
        djui_checkbox_create(body, DLANG(CONTROLS, DISABLE_GAMEPADS), &configDisableGamepads, NULL);
#endif
        djui_checkbox_create(body, DLANG(MISC, USE_STANDARD_KEY_BINDINGS_CHAT), &configUseStandardKeyBindingsChat, NULL);

#ifdef HAVE_SDL2
        int numJoys = SDL_NumJoysticks();
        if (numJoys == 0) { numJoys = 1; }
        if (numJoys > 10) { numJoys = 10; }

        char* gamepadChoices[numJoys];

        // Get the names of all connected gamepads, if none is provided, use "Unknown"
        for (int i = 0; i < numJoys; i++) {
            const char* joystickName = SDL_JoystickNameForIndex(i);
            if (joystickName == NULL) {
                joystickName = "Unknown";
            }
            gamepadChoices[i] = strdup(joystickName);
        }

        // Check for repeated names and append a number if necessary
        for (int i = 0; i < numJoys; i++) {
            int count = 1;
            for (int j = 0; j < i; j++) {
                if (strcmp(gamepadChoices[i], gamepadChoices[j]) == 0) {
                    count++;
                    char newName[256];
                    
                    // If the name is bigger than 9 characters, we need to truncate it first
                    // Then we can append the number so it fits in the slot

                    // Should we look into making scrolling text for this?
                    if (strlen(gamepadChoices[i]) > 9) {
                        snprintf(newName, sizeof(newName), "%.9s... (%d)", gamepadChoices[i], count);
                    } else {
                        snprintf(newName, sizeof(newName), "%s (%d)", gamepadChoices[i], count);
                    }
                    
                    // Remove the old string and replace it with the new one
                    free(gamepadChoices[i]);
                    gamepadChoices[i] = strdup(newName);
                }
            }
        }

        // Create the button
        djui_selectionbox_create(body, DLANG(CONTROLS, GAMEPAD), gamepadChoices, numJoys, &configGamepadNumber, NULL);

        // Free the memory we don't need anymore
        for (int i = 0; i < numJoys; i++) {
            free(gamepadChoices[i]);
        }
#endif

        djui_slider_create(body, DLANG(CONTROLS, DEADZONE), &configStickDeadzone, 0, 100, djui_panel_controls_value_change);
        djui_slider_create(body, DLANG(CONTROLS, RUMBLE_STRENGTH), &configRumbleStrength, 0, 100, djui_panel_controls_value_change);

    #ifdef TOUCH_CONTROLS
        struct DjuiButton* backButton = djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
        djui_base_set_size(&backButton->base, 1.0f, 46);
    #else
        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    #endif
    }

    djui_panel_add(caller, panel, NULL);
}
