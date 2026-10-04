#include "tab5_runtime.hpp"
#include "tab5_field.hpp"
#include "tab5_field_data.hpp"
#include "tab5_display.hpp"
#include "tab5_console.hpp"
#include "tab5_rtc.h"

namespace hro::tab5::app {
namespace {
struct Request { PhoneFieldData data; int64_t received_us; uint32_t id; };
struct Result { uint32_t id; bool clock; bool rtc; };
QueueHandle_t requests = nullptr, results = nullptr;
portMUX_TYPE coordinates_lock = portMUX_INITIALIZER_UNLOCKED;
PhoneFieldData temporary;
bool have_position = false;
std::atomic<bool> phone_clock{false};
uint32_t request_id = 0; // HTTP handlers run on a single server task.

bool same_time(const device::RtcDateTime& a, const tm& b)
{
    return a.date.year == b.tm_year + 1900 && a.date.month == b.tm_mon + 1 &&
        a.date.date == b.tm_mday && a.time.hours == b.tm_hour &&
        a.time.minutes == b.tm_min && a.time.seconds == b.tm_sec;
}

esp_err_t phone_post(httpd_req_t* req)
{
    if (req->content_len < 1 || req->content_len > 512) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected up to 512 bytes of JSON");
    }
    char body[513]{};
    int received = 0;
    while (received < req->content_len) {
        const int count = httpd_req_recv(req, body + received, req->content_len - received);
        if (count <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete JSON request");
        received += count;
    }
    Request request{};
    if (!decode_phone_field_data(body, received, request.data))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid version, UTC unix_ms or coordinates");
    if (!requests || !results || g_hro_shutdown_requested.load(std::memory_order_acquire)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Tab5 is not ready", HTTPD_RESP_USE_STRLEN);
    }
    request.received_us = esp_timer_get_time();
    request.id = ++request_id;
    if (xQueueSend(requests, &request, 0) != pdTRUE) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Field request busy", HTTPD_RESP_USE_STRLEN);
    }
    Result result{};
    const TickType_t start = xTaskGetTickCount();
    bool complete = false;
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(2500)) {
        if (xQueueReceive(results, &result, pdMS_TO_TICKS(50)) == pdTRUE && result.id == request.id) {
            complete = true; break;
        }
    }
    httpd_resp_set_type(req, "application/json");
    if (!complete) {
        httpd_resp_set_status(req, "504 Gateway Timeout");
        return httpd_resp_send(req, "{\"status\":\"pending\"}", HTTPD_RESP_USE_STRLEN);
    }
    if (!result.clock) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "{\"status\":\"clock_update_failed\"}", HTTPD_RESP_USE_STRLEN);
    }
    return httpd_resp_send(req, result.rtc ?
        "{\"status\":\"applied\",\"clock_updated\":true,\"rtc_updated\":true,\"config_saved\":false}" :
        "{\"status\":\"applied\",\"clock_updated\":true,\"rtc_updated\":false,\"config_saved\":false}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t field_page(httpd_req_t* req)
{
    static const char html[] =
        "<!doctype html><html lang='en'><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Tab5 Field Setup</title><style>body{font:18px sans-serif;max-width:650px;margin:30px;padding:10px}</style>"
        "<h1>Tab5 Field Setup</h1><p>Connect your phone to the Tab5 Wi-Fi and run the iPhone shortcut.</p>"
        "<p>Send UTC time, latitude and longitude to <code>/api/field</code>.</p>"
        "<p>The location is used for this session only. config.ini is not changed.</p>"
        "<p>Allow location and local network access when your phone asks.</p>"
        "<p>Check the Tab5 console for <b>Phone time applied</b> and <b>Field location</b>.</p>"
        "<a href='/station'>Saved station settings</a></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}
}
void field_init()
{
    requests = xQueueCreate(1, sizeof(Request));
    results = xQueueCreate(1, sizeof(Result));
    if (!requests || !results) console_message("Phone field setup unavailable", ConsoleLevel::Error);
}

double observation_latitude(double saved)
{
    portENTER_CRITICAL(&coordinates_lock);
    const double value = have_position ? temporary.latitude : saved;
    portEXIT_CRITICAL(&coordinates_lock);
    return value;
}
double observation_longitude(double saved)
{
    portENTER_CRITICAL(&coordinates_lock);
    const double value = have_position ? temporary.longitude : saved;
    portEXIT_CRITICAL(&coordinates_lock);
    return value;
}
bool phone_clock_active() { return phone_clock.load(std::memory_order_relaxed); }
void clear_phone_clock_source() { phone_clock.store(false, std::memory_order_relaxed); }

void poll_phone_field()
{
    Request request{};
    if (!requests || !results || xQueueReceive(requests, &request, 0) != pdTRUE) return;
    // Account for time spent waiting for the LCD task, without using its old clock.
    const int64_t elapsed_us = esp_timer_get_time() - request.received_us;
    if (g_hro_shutdown_requested.load(std::memory_order_acquire) || elapsed_us > 2500000) {
        Result result{request.id, false, false}; xQueueOverwrite(results, &result); return;
    }
    const int64_t unix_us = request.data.unix_ms * 1000 + elapsed_us;
    timeval tv{}; tv.tv_sec = unix_us / 1000000; tv.tv_usec = unix_us % 1000000;
    Result result{request.id, settimeofday(&tv, nullptr) == 0, false};
    if (result.clock) {
        phone_clock.store(true, std::memory_order_relaxed);
        g_ntp_synced.store(false, std::memory_order_relaxed);
        portENTER_CRITICAL(&coordinates_lock);
        temporary = request.data; have_position = true;
        portEXIT_CRITICAL(&coordinates_lock);
        tm utc{}; gmtime_r(&tv.tv_sec, &utc);
        if (device::rtc_enabled()) {
            const device::RtcDateTime value = {
                {static_cast<int16_t>(utc.tm_year + 1900), static_cast<int8_t>(utc.tm_mon + 1), static_cast<int8_t>(utc.tm_mday)},
                {static_cast<int8_t>(utc.tm_hour), static_cast<int8_t>(utc.tm_min), static_cast<int8_t>(utc.tm_sec)}};
            device::write_rtc(value);
            const auto readback = device::read_rtc();
            const time_t next = tv.tv_sec + 1; tm utc_next{}; gmtime_r(&next, &utc_next);
            result.rtc = same_time(readback, utc) || same_time(readback, utc_next);
        }
        console_message("Phone time applied (UTC)");
        console_message(result.rtc ? "RTC updated from phone" : "Phone time applied; RTC update unavailable",
                        result.rtc ? ConsoleLevel::Info : ConsoleLevel::Warning);
        console_printf(ConsoleLevel::Info, "Field location: %.6f, %.6f (temporary)", request.data.latitude, request.data.longitude);
        draw_station_info();
        draw_current_time();
    } else console_message("Phone time update failed", ConsoleLevel::Error);
    xQueueOverwrite(results, &result);
}

esp_err_t register_field_handlers(httpd_handle_t server)
{
    const httpd_uri_t post = {.uri="/api/field", .method=HTTP_POST, .handler=phone_post, .user_ctx=nullptr};
    const httpd_uri_t page = {.uri="/field", .method=HTTP_GET, .handler=field_page, .user_ctx=nullptr};
    esp_err_t err = httpd_register_uri_handler(server, &post);
    return err == ESP_OK ? httpd_register_uri_handler(server, &page) : err;
}
}
