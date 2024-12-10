#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_menu_options.h"
#include "djui_panel_options.h"
#include "djui_panel_language.h"
#include "djui_panel_info.h"
#include "pc/utils/misc.h"
#include "pc/configfile.h"
#include "game/hardcoded.h"

void djui_panel_touchcontrol_set_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(MISC, MISC_TITLE), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        djui_slider_create(body, "Touch Controls Red", &configTouchControlRed, 1, 255, NULL);
        djui_slider_create(body, "Touch Controls Green", &configTouchControlGreen, 1, 255, NULL);
        djui_slider_create(body, "Touch Controls Blue", &configTouchControlBlue, 1, 255, NULL);
        djui_slider_create(body, "Touch Controls Opacity", &configTouchControlAlpha, 1, 255, NULL);
        djui_checkbox_create(body, "Bigger Touch Controls", &configAndroidBiggerButtons, NULL);

        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    djui_panel_add(caller, panel, NULL);
}
