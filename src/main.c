#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"                   // LVGL
#include "esp_lcd_ili9341.h"        // LCD
#include "esp_lcd_touch_xpt2046.h"  // TOUCH

// PIN-KONFIGURATION
#define LCD_HOST       SPI3_HOST // 
#define PIN_MOSI       23      // SPI ("SDI" - data to display)
#define PIN_MISO       19      // SPI ("SDO" - data from display)
#define PIN_SCK        18      // SPI (Clock for sync)
#define PIN_CS         5       // Chip select
#define PIN_DC         2       // Data / Command
#define PIN_RESET      4       // Display reset
#define PIN_T_CS       22      // Touch Chip select 



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





/*
// Globalt display-handtag för callback
static lv_display_t * disp_global = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;

// Callback: Touchfunktion
static void lvgl_touch_cb(lv_indev_t * indev, lv_indev_data_t * data) {
    if (!tp_handle) return; 
    esp_lcd_touch_point_data_t point;
    uint8_t touch_cnt = 0;

    // Läs data från chippet in i intern buffert
    esp_lcd_touch_read_data(tp_handle);

    // Hämta ut data från bufferten till vår struct
    esp_err_t err = esp_lcd_touch_get_data(tp_handle, &point, &touch_cnt, 1);

    if (err == ESP_OK && touch_cnt > 0) {
        data->point.x = point.x;
        data->point.y = point.y;
        data->state = LV_INDEV_STATE_PRESSED;
        // Debug för att se koordinater i terminalen:
        // printf("Touch: X=%d, Y=%d\n", point.x, point.y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}


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

// Event för touchknappen
static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        printf("Button pressed!\n");
        // Här kan du lägga till vad som ska hända
    }
}

void app_main() {
    // 1. Tvinga CS höga direkt
    gpio_reset_pin(PIN_CS);
    gpio_reset_pin(PIN_T_CS);
    gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_T_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_CS, 1);
    gpio_set_level(PIN_T_CS, 1);

    // 1. Initiera SPI-bussen
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = 19, // -1 (change to 19 for touch)
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 80 * sizeof(uint16_t),
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_SCLK | SPICOMMON_BUSFLAG_MISO | SPICOMMON_BUSFLAG_MOSI,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // Minska störningar på långa kablar
    gpio_set_drive_capability(PIN_SCK,  GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability(PIN_MOSI, GPIO_DRIVE_CAP_0);

    // 2. Initiera LVGL (Måste göras tidigt för att skapa display-objektet)
    lv_init();

    // 3. Konfigurera Panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = 8 * 1000 * 1000, // styr snabbhetet. Högre=snabbare men mindre stabilt
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = disp_global, 
        .cs_ena_pretrans = 2, // Ger displayen tid att reagera på CS 
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


    // GE DISPLAYEN TID ATT STARTA
    vTaskDelay(pdMS_TO_TICKS(100)); 


    // --- Initiera Touch ---
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config = {
        .cs_gpio_num = PIN_T_CS,
        .dc_gpio_num = -1,    // -1 to deactivate touch.
        .spi_mode = 0,
        .pclk_hz = 2 * 1000 * 1000, 
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 240,
        .y_max = 320,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    // Kräver komponenten esp_lcd_touch_xpt2046
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &tp_handle));

    // --- STEG 3: Registrera Touch i LVGL ---
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);


    // Inställningar för orientering
    esp_lcd_panel_mirror(panel_handle, false, true);
    esp_lcd_panel_swap_xy(panel_handle, false);
    esp_lcd_panel_set_gap(panel_handle, 0, 0);

    // 5. Slutför LVGL-konfiguration
    disp_global = lv_display_create(240, 320);
    lv_display_set_user_data(disp_global, panel_handle);
    lv_display_set_flush_cb(disp_global, lvgl_flush_cb);
    lv_display_set_color_format(disp_global, LV_COLOR_FORMAT_RGB565);

    // Allokera DMA-minne för buffert (80 rader för stabilitet)
    void *buf1 = heap_caps_malloc(240 * 40 * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL); //80->40.
    lv_display_set_buffers(disp_global, buf1, NULL, 240 * 40 * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_PARTIAL); //80->40

    // 6. Skapa UI
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_palette_main(LV_PALETTE_AMBER), 0);

    // Button UI
    lv_obj_t * btn = lv_button_create(lv_screen_active());
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Press here");
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL, NULL);

    // UI
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
*/