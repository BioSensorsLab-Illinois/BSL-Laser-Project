#include "laser_controller_app.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *kAppTag = "laser_main";

void app_main(void)
{
    const esp_err_t err = laser_controller_app_start();
    if (err == ESP_OK) {
        return;
    }

    ESP_LOGE(kAppTag, "controller startup failed: %s", esp_err_to_name(err));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

