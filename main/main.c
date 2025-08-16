#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "display.h"

static const char *TAG = "main";

void app_main(void)
{
    display_oled_init();
    display_oled_show_text("字库");
}
