#include "stick_led.hpp"
#include "M5GFX.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

namespace stick {
namespace {
QueueHandle_t notifications = nullptr;
bool set_led(bool on) {
    // Official M5PM1_Class::setLedEnLevel: PWR_CFG 0x06, LED_EN bit4.
    // StickS3 schematic LED_EN -> PY_STATUS_LED (active high).
    return (on ? lgfx::i2c::bitOn(1, 0x6e, 0x06, 0x10, 100000)
               : lgfx::i2c::bitOff(1, 0x6e, 0x06, 0x10, 100000)).has_value();
}
void task(void*) {
    unsigned count;
    for (;;) {
        if (xQueueReceive(notifications, &count, portMAX_DELAY) != pdTRUE) continue;
        for (unsigned i = 0; i < count; ++i) {
            if (!set_led(true)) ESP_LOGW("stick_led", "LED on failed");
            vTaskDelay(pdMS_TO_TICKS(100));
            if (!set_led(false)) ESP_LOGW("stick_led", "LED off failed");
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
}
bool led_begin() {
    if (!set_led(false)) return false;
    notifications = xQueueCreate(16, sizeof(unsigned));
    if (!notifications) return false;
    if (xTaskCreate(task, "stick_notify_led", 3072, nullptr, 1, nullptr) != pdPASS) {
        vQueueDelete(notifications); notifications = nullptr; return false;
    }
    return true;
}
void led_notify(unsigned count) {
    if (!count) return;
    if (!notifications || xQueueSend(notifications, &count, 0) != pdTRUE)
        ESP_LOGW("stick_led", "LED notification unavailable/full");
}
}
