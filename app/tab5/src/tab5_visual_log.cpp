#include "tab5_visual_log.hpp"
#include "tab5_visual_csv_upgrade.hpp"
#include "tab5_console.hpp"
#include "tab5_sdcard.h"
#include "tab5_png.hpp"
#include <sys/stat.h>
#include <cerrno>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>
#include <unistd.h>

namespace hro::tab5::app {
namespace {
constexpr const char* tag = "Tab5-VISUAL-SD";
bool record_path(const VisualLogRecord& record, char* path, std::size_t size) {
    char filename[64];
    if (record.utc_ms) {
        char png[64];
        if (!make_screenshot_filename(visual_csv_block(record), png, sizeof(png)) ||
            !visual_csv_filename(png, filename, sizeof(filename))) return false;
    } else {
        // No valid timestamp exists for choosing a dated 20-minute block.
        std::snprintf(filename, sizeof(filename), "VISUAL_UNSYNC.csv");
    }
    const int n = std::snprintf(path, size, "/sdcard/tab5-hro/%s", filename);
    return n > 0 && static_cast<std::size_t>(n) < size;
}
QueueHandle_t records = nullptr;
SemaphoreHandle_t gate = nullptr;
std::atomic<bool> closing{false}, done{true};
bool append(const VisualLogRecord& record, const char* path) {
    if (!device::sdcard_mounted()) return false;
    if (mkdir("/sdcard/tab5-hro", 0775) != 0 && errno != EEXIST) return false;
    static char checked_path[128] = {};
    if (std::strcmp(checked_path, path)) {
        if (!visual_csv_upgrade(path)) return false;
        std::snprintf(checked_path, sizeof(checked_path), "%s", path);
    }
    char row[160];
    const int n = visual_csv_row(row, sizeof(row), record);
    if (n < 0) return false;
    FILE* fp = std::fopen(path, "a+");
    if (!fp) return false;
    bool ok = std::fseek(fp, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(fp) : -1;
    ok = ok && size >= 0;
    if (ok && size == 0) ok = std::fputs(VISUAL_CSV_HEADER, fp) >= 0;
    if (ok) ok = std::fwrite(row, 1, n, fp) == static_cast<std::size_t>(n);
    if (ok) ok = std::fflush(fp) == 0 && fsync(fileno(fp)) == 0;
    if (std::fclose(fp) != 0) ok = false;
    // Do not retry a possibly partial write: report the error without duplicating a row.
    return ok;
}
void writer(void*) {
    bool failed = false;
    char announced_path[128] = {};
    for (;;) {
        VisualLogRecord record;
        if (xQueueReceive(records, &record, pdMS_TO_TICKS(100)) == pdTRUE) {
            char path[128] = {};
            if (!record_path(record, path, sizeof(path)) || !append(record, path)) {
                ESP_LOGE(tag, "Save failed; Stick=%s Event=%s", record.request.stick_id, record.request.event_id);
                console_printf(ConsoleLevel::Error, "VISUAL SD save failed: %s %s", record.request.stick_id, record.request.event_id);
                failed = true;
            } else {
                if (failed) console_message("VISUAL SD writing recovered");
                if (std::strcmp(announced_path, path)) {
                    console_printf(ConsoleLevel::Info, "VISUAL SD saved: %s", path + 7);
                    std::snprintf(announced_path, sizeof(announced_path), "%s", path);
                }
                failed = false;
            }
            continue;
        }
        if (closing.load(std::memory_order_acquire)) {
            done.store(true, std::memory_order_release);
            vTaskDelete(nullptr);
        }
    }
}
}
bool visual_log_start() {
    if (records) return true;
    records = xQueueCreate(128, sizeof(VisualLogRecord));
    gate = xSemaphoreCreateMutex();
    done.store(false); closing.store(false);
    if (!records || !gate || xTaskCreate(writer, "visual_sd", 4096, nullptr, 1, nullptr) != pdPASS) {
        if (records) { vQueueDelete(records); records = nullptr; }
        if (gate) { vSemaphoreDelete(gate); gate = nullptr; }
        done.store(true);
        console_message("VISUAL SD writer unavailable", ConsoleLevel::Error);
        return false;
    }
    console_message("VISUAL SD log: 20-minute CSV beside PNG");
    return true;
}
bool visual_log_enqueue(const VisualLogRecord& record) {
    if (!records || !gate || xSemaphoreTake(gate, 0) != pdTRUE) return false;
    const bool ok = !closing.load() && xQueueSend(records, &record, 0) == pdTRUE;
    xSemaphoreGive(gate);
    return ok;
}
bool visual_log_stop() {
    if (!records) return true;
    if (xSemaphoreTake(gate, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    closing.store(true, std::memory_order_release);
    xSemaphoreGive(gate);
    // Called repeatedly by the existing shutdown loop until writing finishes.
    return done.load(std::memory_order_acquire);
}
}
