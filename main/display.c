//
// Created by lsk on 6/27/25.
//

#include "display.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "lvgl.h"

// OLED CONFIGURATIONS
// Will be ported to Kconfig later.
#define LCD_PIXEL_CLOCK_HZ              (400*1000)
#define LCD_PIN_SDA                     41      // SDA connect to GPIO41
#define LCD_PIN_SCL                     42      // SCL connect to GPIO42
#define LCD_PIN_RST                     (-1)    // No such reset pin...
#define I2C_HW_ADDR                     0x3C    // I2C address for SSD1306 is 0x27
#define LCD_H_RES                       128     // Size for the screen
#define LCD_V_RES                       32
#define I2C_BUS_PORT                    0       // Use I2C controller 0

LV_FONT_DECLARE(font_lxwk);     // Font handle
lv_disp_t *oled_handle;                // OLED handle
lv_style_t style;               // Style used to display texts

#define TAG "display"

void display_oled_init()
{
    // Create the i2c bus handle
    ESP_LOGI(TAG, "Initializing OLED...");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = LCD_PIN_SDA,
        .scl_io_num = LCD_PIN_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));
    // Install the SSD1306 driver
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = I2C_HW_ADDR,
        .scl_speed_hz = LCD_PIXEL_CLOCK_HZ,
        .control_phase_bytes = 1,               // According to SSD1306 datasheet
        .lcd_cmd_bits = 8,                      // According to SSD1306 datasheet
        .lcd_param_bits = 8,                    // According to SSD1306 datasheet
        .dc_bit_offset = 6,                     // According to SSD1306 datasheet
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = LCD_PIN_RST,
    };
    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));
    // OLED Initialization
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    // LVGL Initialization
    // LVGL is a graphical library. We use it to draw texts.
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        }
    };
    oled_handle = lvgl_port_add_disp(&disp_cfg);
    lv_disp_set_rotation(oled_handle, LV_DISP_ROT_NONE);  // No rotation
    ESP_LOGI(TAG, "OLED Initialized");

    // Load the font
    lv_style_init(&style);
    lv_style_set_text_font(&style, &font_lxwk);
}

void display_oled_clear()
{
    lv_obj_clean(lv_scr_act());
}

void display_oled_show_text(char* text)
{
    if (lvgl_port_lock(0)) // Lock the mutex due to the LVGL APIs are not thread-safe
    {
        lv_obj_t *scr = lv_disp_get_scr_act(oled_handle);
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR); // Circular scroll
        lv_obj_add_style(label, &style, LV_STATE_DEFAULT);
        lv_label_set_text(label, text);
        lv_obj_set_width(label, oled_handle->driver->hor_res);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
        // Release the mutex
        lvgl_port_unlock();
    }
}