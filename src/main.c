#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "esp_lcd_ili9341.h"

// PIN-KONFIGURATION
#define LCD_HOST       SPI3_HOST
#define PIN_MOSI       23   // SPI ("SDI" - data to display)
#define PIN_MISO       19   // SPI ("SDO" - data from display)
#define PIN_SCK        18   // SPI (Clock for sync)
#define PIN_CS         15   // Chip select
#define PIN_DC         2    // Data / Command
#define PIN_RESET      4    // Display reset

// Globalt display-handtag för callback
static lv_display_t * disp_global = NULL;

// Callback: Anropas av hårdvaran när SPI-överföringen är fysiskt klar
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    if (disp_global) {
        lv_display_flush_ready(disp_global);
    }
    return false;
}

// Flush: Anropas av LVGL för att rita ett område
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Byt bytes (Little -> Big Endian)
    lv_draw_sw_rgb565_swap(px_map, w * h);
    
    // Skicka pixlar till ILI9341
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

void app_main() {
    // 1. Initiera SPI-bussen
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Minska störningar på långa kablar
    gpio_set_drive_capability(PIN_SCK,  GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability(PIN_MOSI, GPIO_DRIVE_CAP_0);

    // 2. Initiera LVGL (Måste göras tidigt för att skapa display-objektet)
    lv_init();
    disp_global = lv_display_create(240, 320);

    // 3. Konfigurera Panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = 20 * 1000 * 1000, // styr snabbhetet. Högre=snabbare men mindre stabilt
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = disp_global, 
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    // 4. Initiera ILI9341 Panel
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // Inställningar för orientering
    esp_lcd_panel_mirror(panel_handle, false, true);
    esp_lcd_panel_swap_xy(panel_handle, false);
    esp_lcd_panel_set_gap(panel_handle, 0, 0);

    // 5. Slutför LVGL-konfiguration
    lv_display_set_user_data(disp_global, panel_handle);
    lv_display_set_flush_cb(disp_global, lvgl_flush_cb);
    lv_display_set_color_format(disp_global, LV_COLOR_FORMAT_RGB565);

    // Allokera DMA-minne för buffert (80 rader för stabilitet)
    void *buf1 = heap_caps_malloc(240 * 80 * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_display_set_buffers(disp_global, buf1, NULL, 240 * 80 * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 6. Skapa UI
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_palette_main(LV_PALETTE_AMBER), 0);

    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hejsan hejsan\nTEST");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    // 7. Loop
    while (1) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}