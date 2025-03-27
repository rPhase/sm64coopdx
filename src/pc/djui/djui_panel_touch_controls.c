#ifdef TOUCH_CONTROLS
#include "djui.h"
#include "djui_panel.h"
#include "djui_panel_menu.h"
#include "djui_panel_options.h"
#include "djui_panel_language.h"
#include "djui_panel_info.h"
#include "pc/configfile.h"
#include "pc/controller/controller_touchscreen.h"

//same things as djui panel player
static struct DjuiSlider *sSliderR = NULL;
static struct DjuiSlider *sSliderG = NULL;
static struct DjuiSlider *sSliderB = NULL;

void djui_panel_touch_controls_create(struct DjuiBase* caller) {
    struct DjuiThreePanel* panel = djui_panel_menu_create(DLANG(TOUCH_CONTROLS, TOUCH_CONTROLS), false);
    struct DjuiBase* body = djui_three_panel_get_body(panel);
    {
        sSliderR = djui_slider_create(body, DLANG(PLAYER, RED), &configTouchControlRed, 0, 255, NULL);
        djui_base_set_color(&sSliderR->rectValue->base, 255, 0, 0, 255);
        sSliderR->updateRectValueColor = false;
        sSliderG = djui_slider_create(body, DLANG(PLAYER, GREEN), &configTouchControlGreen, 0, 255, NULL);
        djui_base_set_color(&sSliderG->rectValue->base, 0, 255, 0, 255);
        sSliderG->updateRectValueColor = false;
        sSliderB = djui_slider_create(body, DLANG(PLAYER, BLUE), &configTouchControlBlue, 0, 255, NULL);
        djui_base_set_color(&sSliderB->rectValue->base, 0, 0, 255, 255);
        sSliderB->updateRectValueColor = false;

        djui_slider_create(body, DLANG(TOUCH_CONTROLS, TOUCH_CONTROLS_OPACITY), &configTouchControlAlpha, 0, 255, NULL);
        djui_slider_create(body, DLANG(TOUCH_CONTROLS, TOUCH_CONTROLS_SCALE), &configAndroidBiggerButtons, 0, 2, NULL);

        djui_checkbox_create(body, DLANG(TOUCH_CONTROLS, TOUCH_AUTOHIDE), &configAutohideTouch, NULL);
        djui_checkbox_create(body, DLANG(TOUCH_CONTROLS, TOUCH_SLIDE_TOUCH), &configSlideTouch, NULL);
        djui_checkbox_create(body, DLANG(TOUCH_CONTROLS, TOUCH_IGNORE_PHANTOMS), &configPhantomTouch, NULL);

        struct DjuiButton* touchbindsButton = djui_button_create(body, DLANG(TOUCH_CONTROLS, TOUCH_EDIT_BINDS), DJUI_BUTTON_STYLE_NORMAL, djui_panel_shutdown_touchconfig);
        djui_base_set_size(&touchbindsButton->base, 1.0f, 43);

        djui_button_create(body, DLANG(MENU, BACK), DJUI_BUTTON_STYLE_BACK, djui_panel_menu_back);
    }

    djui_panel_add(caller, panel, NULL);
}
#endif