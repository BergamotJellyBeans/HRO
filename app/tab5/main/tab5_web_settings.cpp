#include "tab5_runtime.hpp"
#include "tab5_web_settings.hpp"
#include "tab5_helpers.hpp"
#include "hro_sdr_config.h"

namespace hro::tab5::app {



esp_err_t station_get_handler( httpd_req_t *req )
{
    ESP_LOGI( TAG, "Station settings requested" );

    const double frequency_mhz = static_cast<double>( g_hro_config.frequency_hz ) / 1000000.0;
    DmsValue lon = longitude_to_dms( g_hro_config.longitude );
    DmsValue lat = latitude_to_dms( g_hro_config.latitude );

    constexpr size_t GAIN_OPTIONS_SIZE = 2048;
    char* gain_options = static_cast<char*>(heap_caps_calloc(
        1, GAIN_OPTIONS_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!gain_options) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    size_t gain_used = 0;
    for (int gain : hro::SDR_GAIN_VALUES) {
        if (!is_valid_tab5_sdr_gain(gain)) continue;
        const int written = snprintf(gain_options + gain_used,
            GAIN_OPTIONS_SIZE - gain_used,
            "<option value=\"%d\" %s>%.1f</option>", gain,
            gain == g_hro_config.sdr_gain ? "selected" : "", gain / 10.0);
        if (written < 0 || static_cast<size_t>(written) >= GAIN_OPTIONS_SIZE - gain_used) {
            free(gain_options);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Gain list too large");
            return ESP_FAIL;
        }
        gain_used += static_cast<size_t>(written);
    }

    // HTML全体をPSRAM側に確保
    constexpr size_t HTML_SIZE = 16384;

    char *html = static_cast<char *>(heap_caps_malloc( HTML_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT ) );

    if ( !html ) {
        free(gain_options);
        ESP_LOGE( TAG, "Failed to allocate station HTML" );
        httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory" );
        return ESP_FAIL;
    }

    snprintf(
        html,
        HTML_SIZE,

        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\">"

        "<title>Tab5-HRO Station Settings</title>"

        "<style>"

        "body{"
            "font-family:Arial,sans-serif;"
            "max-width:700px;"
            "margin:30px auto;"
            "padding:0 20px;"
            "background:#f5f5f5;"
        "}"

        ".dms{"
            "display:flex;"
            "align-items:center;"
            "gap:6px;"
            "margin-top:5px;"
        "}"

        ".dms input{"
            "width:100px;"
            "margin-top:0;"
        "}"

        ".dms select{"
            "padding:9px;"
            "font-size:16px;"
        "}"

        ".dms span{"
            "font-size:16px;"
        "}"

        "@media(max-width:600px){"
            ".dms input{width:80px;}"
        "}"

        ".card{"
            "background:white;"
            "padding:24px;"
            "border-radius:10px;"
            "box-shadow:0 2px 8px rgba(0,0,0,.15);"
        "}"

        "h1{font-size:24px;}"
        "h2{font-size:18px;margin-top:28px;}"

        "label{"
            "display:block;"
            "margin-top:14px;"
            "font-weight:bold;"
        "}"

        "input{"
            "width:100%%;"
            "box-sizing:border-box;"
            "padding:9px;"
            "margin-top:5px;"
            "font-size:16px;"
        "}"

        ".sdr-gain{"
            "width:100%%;max-width:480px;height:38px;"
            "box-sizing:border-box;padding:7px 12px;"
            "margin-top:5px;font-size:15px;"
            "background:#eee;border:1px solid #ddd;border-radius:6px;"
        "}"

        ".info{"
            "margin-top:16px;"
            "padding:12px;"
            "background:#eef5ff;"
            "border-radius:6px;"
        "}"

        ".error{"
            "margin-top:10px;"
            "color:#c00000;"
            "font-weight:bold;"
            "min-height:20px;"
        "}"

        ".note{"
            "font-size:13px;"
            "color:#666;"
            "margin-top:10px;"
        "}"

        "button{"
            "margin-top:20px;"
            "padding:11px 28px;"
            "font-size:16px;"
            "cursor:pointer;"
        "}"

        "button:disabled{"
            "opacity:0.4;"
            "cursor:not-allowed;"
        "}"

        "</style>"
        "</head>"

        "<body>"
        "<div class=\"card\">"

        "<h1>Tab5-HRO Station Settings</h1>"

        "<form method=\"POST\" action=\"/station/save\">"

        "<h2>Station</h2>"

        "<label>Observer</label>"
        "<input "
            "name=\"observer\" "
            "type=\"text\" "
            "maxlength=\"63\" "
            "value=\"%s\">"

        "<label>Receiving Location</label>"
        "<input "
            "name=\"location\" "
            "type=\"text\" "
            "maxlength=\"127\" "
            "value=\"%s\">"

        "<label>Longitude</label>"
        "<div class=\"dms\">"

        "<select id=\"lon_dir\" name=\"lon_dir\" "
            "onchange=\"validateSettings()\">"
            "<option value=\"E\" %s>E</option>"
            "<option value=\"W\" %s>W</option>"
        "</select>"

        "<input id=\"lon_deg\" name=\"lon_deg\" "
            "type=\"number\" min=\"0\" max=\"180\" step=\"1\" "
            "value=\"%d\" oninput=\"validateSettings()\">"
        "<span>°</span>"

        "<input id=\"lon_min\" name=\"lon_min\" "
            "type=\"number\" min=\"0\" max=\"59\" step=\"1\" "
            "value=\"%d\" oninput=\"validateSettings()\">"
        "<span>'</span>"

        "<input id=\"lon_sec\" name=\"lon_sec\" "
            "type=\"number\" min=\"0\" max=\"59.999999\" step=\"0.001\" "
            "value=\"%.3f\" oninput=\"validateSettings()\">"
        "<span>\"</span>"

        "</div>"

        "<label>Latitude</label>"
        "<div class=\"dms\">"

        "<select id=\"lat_dir\" name=\"lat_dir\" "
            "onchange=\"validateSettings()\">"
            "<option value=\"N\" %s>N</option>"
            "<option value=\"S\" %s>S</option>"
        "</select>"

        "<input id=\"lat_deg\" name=\"lat_deg\" "
            "type=\"number\" min=\"0\" max=\"90\" step=\"1\" "
            "value=\"%d\" oninput=\"validateSettings()\">"
        "<span>°</span>"

        "<input id=\"lat_min\" name=\"lat_min\" "
            "type=\"number\" min=\"0\" max=\"59\" step=\"1\" "
            "value=\"%d\" oninput=\"validateSettings()\">"
        "<span>'</span>"

        "<input id=\"lat_sec\" name=\"lat_sec\" "
            "type=\"number\" min=\"0\" max=\"59.999999\" step=\"0.001\" "
            "value=\"%.3f\" oninput=\"validateSettings()\">"
        "<span>\"</span>"

        "</div>"

        "<h2>Receiver</h2>"

        "<label>Receiver</label>"
        "<input "
            "name=\"receiver\" "
            "type=\"text\" "
            "maxlength=\"63\" "
            "value=\"%s\">"

        "<label>Receiving Frequency (MHz)</label>"
        "<input "
            "id=\"frequency\" "
            "name=\"frequency\" "
            "type=\"number\" "
            "step=\"0.000001\" "
            "value=\"%.6f\" "
            "oninput=\"validateSettings()\">"

        "<label>SDR Gain (dB)</label>"
        "<select class=\"sdr-gain\" name=\"sdr_gain\">%s</select>"
        "<p class=\"note\">SDR gain takes effect after restarting Tab5.</p>"

        "<label>FFT Center Frequency (Hz)</label>"
        "<input "
            "id=\"fft_center\" "
            "name=\"fft_center\" "
            "type=\"number\" "
            "step=\"1\" "
            "value=\"%ld\" "
            "oninput=\"validateSettings()\">"

        "<label>Level Peak Range +/- (Hz)</label>"
        "<input type=\"number\" "
            "name=\"level_average_range_hz\" "
            "min=\"0\" "
            "step=\"1\" "
            "value=\"%ld\">"

        "<label>Receiving Antenna</label>"
        "<input "
            "name=\"antenna\" "
            "type=\"text\" "
            "maxlength=\"127\" "
            "value=\"%s\">"

        "<h2>Screenshot</h2>"

        "<label>File Prefix</label>"
        "<input "
            "id=\"screenshot_prefix\" "
            "name=\"screenshot_prefix\" "
            "type=\"text\" "
            "maxlength=\"8\" "
            "pattern=\"[A-Za-z0-9]{1,8}\" "
            "value=\"%s\" "
            "oninput=\"validateSettings()\">"

        "<p class=\"note\">"
        "1 - 8 alphanumeric characters (A-Z, a-z, 0-9)"
        "</p>"

        "<div id=\"fft_info\" class=\"info\"></div>"
        "<div id=\"error_msg\" class=\"error\"></div>"

        "<button "
            "id=\"save_button\" "
            "type=\"submit\">"
            "Save"
        "</button>"

        "<p class=\"note\">"
        "FFT processing bandwidth: 0 - 1800 Hz"
        "</p>"

        "</form>"
        "</div>"

        "<script>"

        "function validateSettings(){"

            "const lonDeg=Number(document.getElementById('lon_deg').value);"
            "const lonMin=Number(document.getElementById('lon_min').value);"
            "const lonSec=Number(document.getElementById('lon_sec').value);"

            "const latDeg=Number(document.getElementById('lat_deg').value);"
            "const latMin=Number(document.getElementById('lat_min').value);"
            "const latSec=Number(document.getElementById('lat_sec').value);"

            "const frequency="
                "Number(document.getElementById('frequency').value);"

            "const center="
                "Number(document.getElementById('fft_center').value);"

            "const range=%d;"

            "const prefix="
                "document.getElementById('screenshot_prefix').value;"

            "const info="
                "document.getElementById('fft_info');"

            "const error="
                "document.getElementById('error_msg');"

            "const save="
                "document.getElementById('save_button');"

            "let message='';"

            "const minHz=center-range;"
            "const maxHz=center+range;"
            "const bins=maxHz-minHz+1;"

            "if(!Number.isInteger(lonDeg)||lonDeg<0||lonDeg>180){"
                "message='Longitude degrees must be between 0 and 180';"

            "}else if(!Number.isInteger(lonMin)||lonMin<0||lonMin>59){"
                "message='Longitude minutes must be between 0 and 59';"

            "}else if(!Number.isFinite(lonSec)||lonSec<0||lonSec>=60){"
                "message='Longitude seconds must be between 0 and 59.999';"

            "}else if(lonDeg===180&&(lonMin!==0||lonSec!==0)){"
                "message='At 180 degrees, longitude minutes and seconds must be 0';"

            "}else if(!Number.isInteger(latDeg)||latDeg<0||latDeg>90){"
                "message='Latitude degrees must be between 0 and 90';"

            "}else if(!Number.isInteger(latMin)||latMin<0||latMin>59){"
                "message='Latitude minutes must be between 0 and 59';"

            "}else if(!Number.isFinite(latSec)||latSec<0||latSec>=60){"
                "message='Latitude seconds must be between 0 and 59.999';"

            "}else if(latDeg===90&&(latMin!==0||latSec!==0)){"
                "message='At 90 degrees, latitude minutes and seconds must be 0';"

            "}else if(!Number.isFinite(frequency)||"
                "frequency < 1 || frequency > 2000){"

                "message='Receiving Frequency must be between 1 and 2000 MHz';"

            "}else if(!Number.isInteger(center)||center < 0){"

                "message='FFT Center must be 0 Hz or greater';"

            "}else if(minHz < 0){"

                "message='FFT display lower frequency must be 0 Hz or greater';"

            "}else if(maxHz > 1800){"

                "message='FFT display upper frequency must not exceed 1800 Hz';"

            "}else if(maxHz > 1800){"

                "message='FFT display upper frequency must not exceed 1800 Hz';"

            "}else if(!/^[A-Za-z0-9]{1,8}$/.test(prefix)){"

                "message='Screenshot File Prefix must be 1 to 8 alphanumeric characters';"

            "}"

            "if(Number.isFinite(minHz)&&"
                "Number.isFinite(maxHz)&&"
                "Number.isFinite(bins)){"

                "info.innerHTML="
                    "'FFT Display: '+minHz+' - '+maxHz+' Hz<br>'"
                    "+'Bins: '+bins;"

            "}else{"

                "info.innerHTML='FFT Display: ---';"

            "}"

            "error.textContent=message;"
            "save.disabled=(message!=='');"

        "}"

        "validateSettings();"

        "</script>"

        "</body>"
        "</html>",

        g_hro_config.observer,
        g_hro_config.location,

        ( lon.direction == 'E' ) ? "selected" : "",
        ( lon.direction == 'W' ) ? "selected" : "",
        lon.degrees,
        lon.minutes,
        lon.seconds,

        ( lat.direction == 'N' ) ? "selected" : "",
        ( lat.direction == 'S' ) ? "selected" : "",
        lat.degrees,
        lat.minutes,
        lat.seconds,

        g_hro_config.receiver,
        frequency_mhz,

        gain_options,
        static_cast<long>( g_hro_config.fft_center_hz ),
        static_cast<long>( g_hro_config.level_average_range_hz ),

        g_hro_config.antenna,
        g_hro_config.screenshot_prefix,
        hro::FFT_RANGE_HZ
    );

    free(gain_options);
    httpd_resp_set_type( req, "text/html; charset=utf-8" );

    esp_err_t result = httpd_resp_send( req, html, HTTPD_RESP_USE_STRLEN );

    free(html);

    return result;
}

esp_err_t station_save_handler( httpd_req_t *req )
{
    ESP_LOGI( TAG, "Station settings save requested" );

    if ( req->content_len <= 0 || req->content_len > 4096 ) {
        httpd_resp_send_err( req, HTTPD_400_BAD_REQUEST, "Invalid form data" );
        return ESP_FAIL;
    }

    char *body = static_cast<char *>( malloc( req->content_len + 1 ) );

    if ( !body ) {
        httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory" );
        return ESP_FAIL;
    }

    size_t received = 0;
    while ( received < req->content_len ) {
        int ret = httpd_req_recv( req, body + received, req->content_len - received );
        if ( ret <= 0 ) {
            free( body );

            httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed receiving form data" );
            return ESP_FAIL;
        }
        received += ret;
    }

    body[received] = '\0';

    // 現在値をベースに一時設定を作る
    Tab5Config new_config = g_hro_config;

    char value[256];

    // -------------------------
    // Station
    // -------------------------


    if ( get_form_value( body, "observer", value, sizeof( value ) ) ) {
        snprintf( new_config.observer, sizeof( new_config.observer ), "%.*s", static_cast<int>( sizeof( new_config.observer ) - 1 ), value );
    }
    if ( get_form_value( body, "location", value, sizeof( value ) ) ) {
        snprintf( new_config.location, sizeof( new_config.location ), "%.*s", static_cast<int>( sizeof( new_config.location ) - 1 ), value );
    }

    char lon_dir[8] = "E";
    char lat_dir[8] = "N";

    int lon_deg = 0;
    int lon_min = 0;
    double lon_sec = 0.0;

    int lat_deg = 0;
    int lat_min = 0;
    double lat_sec = 0.0;

    if ( get_form_value( body, "lon_dir", value, sizeof( value ) ) ) {
        snprintf( lon_dir, sizeof( lon_dir ), "%.1s", value );
    }
    if ( get_form_value( body, "lon_deg", value, sizeof( value ) ) ) {
        lon_deg = static_cast<int>( strtol( value, nullptr, 10 ) );
    }
    if ( get_form_value( body, "lon_min", value, sizeof( value ) ) ) {
        lon_min = static_cast<int>( strtol( value, nullptr, 10 ) );
    }
    if ( get_form_value( body, "lon_sec", value, sizeof( value ) ) ) {
        lon_sec = strtod(value, nullptr);
    }

    if ( get_form_value( body, "lat_dir", value, sizeof( value ) ) ) {
        snprintf( lat_dir, sizeof( lat_dir ), "%.1s", value );
    }
    if ( get_form_value( body, "lat_deg", value, sizeof( value ) ) ) {
        lat_deg = static_cast<int>( strtol( value, nullptr, 10 ) );
    }
    if ( get_form_value( body, "lat_min", value, sizeof( value ) ) ) {
        lat_min = static_cast<int>( strtol( value, nullptr, 10 ) );
    }
    if ( get_form_value( body, "lat_sec", value, sizeof( value ) ) ) {
        lat_sec = strtod( value, nullptr );
    }

    if ( lon_deg < 0 || lon_deg > 180 || lon_min < 0 || lon_min > 59 || lon_sec < 0.0 || lon_sec >= 60.0 ||
        ( lon_deg == 180 && ( lon_min != 0 || lon_sec != 0.0 ) ) ) {
        free(body);

        httpd_resp_send_err( req, HTTPD_400_BAD_REQUEST, "Invalid longitude" );
        return ESP_FAIL;
    }

    if ( lat_deg < 0 || lat_deg > 90 || lat_min < 0 || lat_min > 59 || lat_sec < 0.0 || lat_sec >= 60.0 ||
        (lat_deg == 90 && ( lat_min != 0 || lat_sec != 0.0 ) ) ) {
        free(body);

        httpd_resp_send_err( req, HTTPD_400_BAD_REQUEST, "Invalid latitude" );
        return ESP_FAIL;
    }

    if ( ( lon_dir[0] != 'E' && lon_dir[0] != 'W' ) || ( lat_dir[0] != 'N' && lat_dir[0] != 'S' ) ) {
        free(body);

        httpd_resp_send_err( req, HTTPD_400_BAD_REQUEST, "Invalid coordinate direction" );
        return ESP_FAIL;
    }

    double longitude = static_cast<double>( lon_deg ) + static_cast<double>( lon_min ) / 60.0 + lon_sec / 3600.0;
    if ( lon_dir[0] == 'W' ) {
        longitude = -longitude;
    }

    double latitude = static_cast<double>( lat_deg ) + static_cast<double>( lat_min ) / 60.0 + lat_sec / 3600.0;
    if ( lat_dir[0] == 'S' ) {
        latitude = -latitude;
    }

    new_config.longitude = longitude;
    new_config.latitude  = latitude;

    // -------------------------
    // Receiver
    // -------------------------
    if ( get_form_value( body, "receiver", value, sizeof( value ) ) ) {
        snprintf( new_config.receiver, sizeof( new_config.receiver ), "%.*s", static_cast<int>( sizeof( new_config.receiver ) - 1), value );
    }
    if ( get_form_value( body, "antenna", value, sizeof( value ) ) ) {
        snprintf( new_config.antenna, sizeof( new_config.antenna ), "%.*s", static_cast<int>( sizeof( new_config.antenna ) - 1 ), value );
    }

    // Web側はMHzなのでHzへ変換
    if ( get_form_value( body, "frequency", value, sizeof( value ) ) ) {
        double mhz = strtod( value, nullptr );
        new_config.frequency_hz = static_cast<uint32_t>( mhz * 1000000.0 + 0.5 );
    }
    if ( get_form_value( body, "fft_center", value, sizeof( value ) ) ) {
        new_config.fft_center_hz = static_cast<int32_t>( strtol( value, nullptr, 10 ) );
    }
    new_config.fft_range_hz = hro::FFT_RANGE_HZ;
    if (get_form_value(body, "sdr_gain", value, sizeof(value))) {
        char* end = nullptr;
        const long gain = strtol(value, &end, 10);
        if (end == value || *end != '\0' || gain < 0 || gain > 496 ||
            !is_valid_tab5_sdr_gain(static_cast<int>(gain))) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SDR gain");
            return ESP_FAIL;
        }
        new_config.sdr_gain = static_cast<int>(gain);
    }
    if ( get_form_value( body, "level_average_range_hz", value, sizeof( value ) ) ) {
        new_config.level_average_range_hz = static_cast<int32_t>( strtol( value, nullptr, 10 ) );
    }

    // -------------------------
    // Screenshot
    // -------------------------
    if ( get_form_value( body, "screenshot_prefix", value, sizeof( value ) ) ) {
        snprintf( new_config.screenshot_prefix, sizeof( new_config.screenshot_prefix ), "%.*s", static_cast<int>( sizeof( new_config.screenshot_prefix ) - 1 ), value );
    }

    free( body );

    // -------------------------
    // C++側でも再検証
    // -------------------------
    char error_msg[128];

    if ( !validate_hro_config( new_config, error_msg, sizeof( error_msg ) ) ) {
        ESP_LOGW( TAG, "Station settings rejected: %s", error_msg );
        httpd_resp_send_err( req, HTTPD_400_BAD_REQUEST, error_msg );
        return ESP_FAIL;
    }

    // -------------------------
    // 一時ファイルへ保存
    // -------------------------
    if ( !write_hro_config_file( HRO_CONFIG_TEMP_FILE, new_config ) ) {
        httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed writing configuration" );
        return ESP_FAIL;
    }

    // -------------------------
    // config.ini と入れ替え
    // -------------------------
    if ( remove( HRO_CONFIG_FILE ) != 0 ) {
        // ファイルが存在しない場合以外は警告
        if ( errno != ENOENT ) {
            ESP_LOGE( TAG, "Failed removing old config: errno=%d", errno );
            remove( HRO_CONFIG_TEMP_FILE );
            httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed replacing configuration" );
            return ESP_FAIL;
        }
    }

    if ( rename( HRO_CONFIG_TEMP_FILE, HRO_CONFIG_FILE ) != 0 ) {
        ESP_LOGE( TAG, "Failed renaming config: errno=%d", errno );
        httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed replacing configuration" );
        return ESP_FAIL;
    }

    // 全処理成功後に現在設定を更新
    g_hro_config = new_config;

    ESP_LOGI( TAG, "Station settings saved" );

    ESP_LOGI( TAG, "HRO: RF=%lu Hz FFT center=%ld Hz range=+/- %ld Hz", static_cast<unsigned long>( g_hro_config.frequency_hz ), static_cast<long>( g_hro_config.fft_center_hz ), static_cast<long>( g_hro_config.fft_range_hz ) );

    // 保存後 /station へ戻す
    httpd_resp_set_status( req, "303 See Other" );
    httpd_resp_set_hdr( req, "Location", "/station" );
    httpd_resp_send( req, nullptr, 0 );

    return ESP_OK;
}

} // namespace hro::tab5::app
