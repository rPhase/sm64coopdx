#ifdef TOUCH_CONTROLS
#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_options.h"
#include "djui_panel_language.h"
#include "djui_panel_info.h"
#include "pc/configfile.h"

void djui_panel_touchcontrol_settings_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(CONTROLS, TOUCH_BINDS_GRAPHICS), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        djui_slider_create(body, DLANG(CONTROLS, TOUCH_BINDS_RED), &configTouchControlRed, 0, 255, NULL);
        djui_slider_create(body, DLANG(CONTROLS, TOUCH_BINDS_GREEN), &configTouchControlGreen, 0, 255, NULL);
        djui_slider_create(body, DLANG(CONTROLS, TOUCH_BINDS_BLUE), &configTouchControlBlue, 0, 255, NULL);
        djui_slider_create(body, DLANG(CONTROLS, TOUCH_BINDS_ALPHA), &configTouchControlAlpha, 0, 255, NULL);
        djui_slider_create(body, DLANG(CONTROLS, TOUCH_BINDS_SIZE), &configAndroidBiggerButtons, 0, 2, NULL);

        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    djui_panel_add(caller, panel, NULL);
}
#endif