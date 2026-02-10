#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    // Display initiering
    display_init();

    // UI-DESIGN
    // Hämtar den aktiva skärmen och sätter bakgrundsfärg
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_palette_main(LV_PALETTE_AMBER), 0);

    // UI: Lable "text"
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Testar teatar!\nESP32->ili9341");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    // UI: Button
    lv_obj_t *button = lv_button_create(scr);
    lv_obj_t * btn_label = lv_label_create(button);
    lv_label_set_text(btn_label, "Press me");
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -10); 

    while (1) {
        // Hanterar LVGL:s interna timers och anropar flush_cb vid behov
        lv_timer_handler();

        // Ger FreeRTOS tid för andra uppgifter och sparar ström
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
