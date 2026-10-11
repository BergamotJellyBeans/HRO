#include "tab5_field.hpp"
#include "tab5_console.hpp"
#include "tab5_runtime.hpp"
#include "tab5_wifi.hpp"
#include "tab5_helpers.hpp"
#include "tab5_time.hpp"
#include "tab5_web_settings.hpp"

namespace hro::tab5::app {
static bool g_wifi_setup_mode = false;
static std::atomic<bool> g_sta_ready{false};
bool wifi_sta_ready() { return g_sta_ready.load(std::memory_order_acquire); }
static esp_err_t save_wifi_settings( const char *ssid, const char *password );
static void wifi_event_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data );
static esp_err_t wifi_connect_sta( const char *ssid, const char *password );
static esp_err_t wifi_setup_root_handler( httpd_req_t *req );
static esp_err_t wifi_connect_handler( httpd_req_t *req );
static httpd_handle_t start_wifi_setup_webserver( void );

static esp_err_t save_wifi_settings( const char *ssid, const char *password )
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open( "wifi", NVS_READWRITE, &handle );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "nvs_open failed: %s", esp_err_to_name( err ) );
        return err;
    }

    err = nvs_set_str( handle, "ssid", ssid );
    if ( err == ESP_OK ) {
        err = nvs_set_str( handle, "password", password );
    }
    if ( err == ESP_OK ) {
        err = nvs_commit( handle );
    }
    nvs_close( handle );

    if ( err == ESP_OK ) {
        ESP_LOGI( TAG, "Wi-Fi settings saved to NVS" );
    } else {
        ESP_LOGE( TAG, "Failed to save Wi-Fi settings: %s", esp_err_to_name( err ) );
    }
    return err;
}

static void wifi_event_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
    if ( event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START ) {
        ESP_LOGI( TAG, "Wi-Fi STA started" );
    } else if ( event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED ) {
        ESP_LOGI( TAG, "Wi-Fi STA connected" );
    } else if ( event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED ) {
        g_sta_ready.store(false, std::memory_order_release);
        auto *event = static_cast<wifi_event_sta_disconnected_t *>( event_data );
        ESP_LOGW( TAG, "Wi-Fi STA disconnected, reason=%d", event->reason );
        console_printf(ConsoleLevel::Warning, "Wi-Fi disconnected (reason=%d); %s", event->reason,
                       g_wifi_setup_mode ? "setup mode active" : "reconnecting");
        if ( !g_wifi_setup_mode ) {
            ESP_LOGI( TAG, "Reconnecting Wi-Fi STA..." );
            esp_err_t err = esp_wifi_connect();
            if ( err != ESP_OK ) {
                ESP_LOGW( TAG, "esp_wifi_connect failed: %s", esp_err_to_name( err ) );
            }
        } else {
            ESP_LOGI( TAG, "Wi-Fi setup mode: STA reconnect disabled" );
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        const auto* event = static_cast<const wifi_event_ap_staconnected_t*>(event_data);
        console_printf(ConsoleLevel::Info, "AP client connected: ID %02X%02X%02X",
                       static_cast<unsigned>(event->mac[3]), static_cast<unsigned>(event->mac[4]),
                       static_cast<unsigned>(event->mac[5]));
        ESP_LOGI(TAG, "AP client connected: MAC=%02X:%02X:%02X:%02X:%02X:%02X AID=%u",
                 static_cast<unsigned>(event->mac[0]), static_cast<unsigned>(event->mac[1]),
                 static_cast<unsigned>(event->mac[2]), static_cast<unsigned>(event->mac[3]),
                 static_cast<unsigned>(event->mac[4]), static_cast<unsigned>(event->mac[5]),
                 static_cast<unsigned>(event->aid));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const auto* event = static_cast<const wifi_event_ap_stadisconnected_t*>(event_data);
        console_printf(ConsoleLevel::Warning, "AP client disconnected: ID %02X%02X%02X",
                       static_cast<unsigned>(event->mac[3]), static_cast<unsigned>(event->mac[4]),
                       static_cast<unsigned>(event->mac[5]));
        ESP_LOGI(TAG, "AP client disconnected: ID=%02X%02X%02X",
                 static_cast<unsigned>(event->mac[3]), static_cast<unsigned>(event->mac[4]),
                 static_cast<unsigned>(event->mac[5]));
    } else if ( event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP ) {
        auto *event = static_cast<ip_event_got_ip_t *>( event_data );
        ESP_LOGI( TAG, "Wi-Fi STA got IP: " IPSTR, IP2STR( &event->ip_info.ip ) );
        g_sta_ready.store(true, std::memory_order_release);
        console_printf(ConsoleLevel::Info, "Wi-Fi connected: " IPSTR, IP2STR(&event->ip_info.ip));
        start_ntp();	// NTPサーバから日付時刻取得
    }
}

static esp_err_t wifi_connect_sta( const char *ssid, const char *password )
{
    wifi_config_t sta_config = {};

    strncpy( reinterpret_cast<char *>( sta_config.sta.ssid ), ssid, sizeof( sta_config.sta.ssid ) - 1 );
    strncpy( reinterpret_cast<char *>( sta_config.sta.password ), password, sizeof( sta_config.sta.password ) - 1 );

    ESP_LOGI( TAG, "Connecting STA to SSID=%s", ssid );

    esp_err_t err = esp_wifi_set_config( WIFI_IF_STA, &sta_config );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "esp_wifi_set_config(STA) failed: %s", esp_err_to_name( err ) );
        return err;
    }

    err = esp_wifi_connect();
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "esp_wifi_connect failed: %s", esp_err_to_name( err ) );
    }
    return err;
}

bool load_wifi_settings( char *ssid, size_t ssid_size, char *password, size_t password_size )
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open( "wifi", NVS_READONLY, &handle );
    if ( err != ESP_OK ) {
        ESP_LOGI( TAG, "No saved Wi-Fi settings" );
        return false;
    }

    size_t ssid_len = ssid_size;
    err = nvs_get_str( handle, "ssid", ssid, &ssid_len );
    if ( err != ESP_OK ) {
        nvs_close( handle );
        ESP_LOGI( TAG, "No saved Wi-Fi SSID" );
        return false;
    }

    size_t password_len = password_size;
    err = nvs_get_str( handle, "password", password, &password_len );
    if ( err == ESP_ERR_NVS_NOT_FOUND ) {
        // Open network
        password[0] = '\0';
        err = ESP_OK;
    }
    nvs_close( handle );

    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to load Wi-Fi settings: %s", esp_err_to_name( err ) );
        return false;
    }

    ESP_LOGI( TAG, "Saved Wi-Fi settings found: SSID=%s", ssid );
    ESP_LOGI( TAG, "Saved password length: %u", static_cast<unsigned>( strlen( password ) ) );

    return true;
}

static esp_err_t wifi_setup_root_handler( httpd_req_t *req )
{
    ESP_LOGI(TAG, "Wi-Fi scan requested");
    if ( !g_wifi_setup_mode ) {
        ESP_LOGI( TAG, "Entering Wi-Fi setup mode" );
        g_wifi_setup_mode = true;
        // 旧APへの接続動作を停止
        esp_err_t err = esp_wifi_disconnect();
        if ( err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT ) {
            ESP_LOGW( TAG, "esp_wifi_disconnect: %s", esp_err_to_name( err ) );
        }
        // Hosted/C6側の状態遷移が完了するのを少し待つ
        vTaskDelay( pdMS_TO_TICKS( 500 ) );
    }

    wifi_scan_config_t scan_config = {};
    esp_err_t err = esp_wifi_scan_start( &scan_config, true );      // blocking scan
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Wi-Fi scan failed: %s", esp_err_to_name( err ) );
        httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi scan failed" );
        return err;
    }

    uint16_t ap_count = 0;

    ESP_ERROR_CHECK( esp_wifi_scan_get_ap_num( &ap_count ) );

    ESP_LOGI( TAG, "Wi-Fi scan found %u APs", ap_count );

    // 表示数を制限
    if ( ap_count > 30 ) {
        ap_count = 30;
    }

    wifi_ap_record_t *records = nullptr;
    if ( ap_count > 0 ) {
        records = static_cast<wifi_ap_record_t *>( calloc( ap_count, sizeof( wifi_ap_record_t ) ) );
        if ( records == nullptr ) {
            httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory" );
            return ESP_ERR_NO_MEM;
        }

        uint16_t number = ap_count;
        err = esp_wifi_scan_get_ap_records( &number, records );
        if ( err != ESP_OK ) {
            free( records );
            httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot read scan results" );
            return err;
        }
        ap_count = number;
    }

    httpd_resp_set_type( req, "text/html; charset=utf-8" );

    // chunked response
    httpd_resp_sendstr_chunk(
        req,
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\">"
        "<title>Tab5-HRO Wi-Fi Setup</title>"
        "</head>"
        "<body>"
        "<h1>Tab5-HRO Wi-Fi Setup</h1>"
        "<p>Select a Wi-Fi network:</p>"
        "<form method=\"POST\" action=\"/connect\">"
        "<select name=\"ssid\">" );


    for ( uint16_t i = 0; i < ap_count; ++i ) {
        // 空SSID（hidden network）は今回は表示しない
        if ( records[i].ssid[0] == '\0' ) {
            continue;
        }

        char option[192];
        snprintf( option, sizeof( option ),
            "<option value=\"%s\">"
            "%s (%d dBm)"
            "</option>",
            reinterpret_cast<char *>( records[i].ssid ),
            reinterpret_cast<char *>( records[i].ssid ),
            records[i].rssi );

        httpd_resp_sendstr_chunk( req, option );
    }

    httpd_resp_sendstr_chunk(
        req,
        "</select>"
        "<p>"
        "Password:<br>"
        "<input type=\"password\" "
        "name=\"password\" "
        "maxlength=\"64\">"
        "</p>"
        "<p>"
        "<button type=\"submit\">"
        "Connect"
        "</button>"
        "</p>"
        "</form>"
        "<hr>"
        "<p>"
        "<button onclick=\"location.reload()\">"
        "Scan Again"
        "</button>"
        "</p>"
        "</body>"
        "</html>" );

    // chunked response終了
    httpd_resp_sendstr_chunk( req, nullptr );
    free( records );

    return ESP_OK;
}

static esp_err_t wifi_connect_handler( httpd_req_t *req )
{
    ESP_LOGI( TAG, "Wi-Fi connect form received" );

    const int total_len = req->content_len;
    if ( total_len <= 0 || total_len >= 512 ) {
        httpd_resp_send_err( req, HTTPD_400_BAD_REQUEST, "Invalid request" );
        return ESP_FAIL;
    }

    char content[512];
    int received = 0;

    while ( received < total_len ) {
        int ret = httpd_req_recv( req, content + received, total_len - received );
        if ( ret <= 0 ) {
            if ( ret == HTTPD_SOCK_ERR_TIMEOUT ) {
                continue;
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    content[received] = '\0';

    char ssid_encoded[128] = {};
    char password_encoded[256] = {};

    char ssid[33] = {};
    char password[65] = {};

    esp_err_t ssid_result = httpd_query_key_value( content, "ssid", ssid_encoded, sizeof( ssid_encoded ) );
    esp_err_t password_result = httpd_query_key_value( content, "password", password_encoded, sizeof( password_encoded ) );

    if ( ssid_result != ESP_OK ) {
        httpd_resp_send_err( req, HTTPD_400_BAD_REQUEST, "SSID missing" );
        return ESP_FAIL;
    }

    if ( password_result != ESP_OK ) {
        password_encoded[0] = '\0';
    }

    url_decode( ssid, sizeof( ssid ), ssid_encoded );
    url_decode( password, sizeof( password ), password_encoded );

    ESP_LOGI( TAG, "Selected SSID: %s", ssid );

    // Passwordそのものはログに出さない
    ESP_LOGI( TAG, "Password length: %u", static_cast<unsigned>( strlen( password ) ) );

    esp_err_t save_err = save_wifi_settings( ssid, password );
    if ( save_err != ESP_OK ) {
        httpd_resp_send_err( req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save Wi-Fi settings" );
        return save_err;
    }

    static const char response[] =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\">"
        "<title>Tab5-HRO</title>"
        "</head>"
        "<body>"
        "<h1>Wi-Fi Settings Received</h1>"
        "<p>The Wi-Fi settings were received "
        "by Tab5-HRO.</p>"
        "<p>No connection has been attempted yet.</p>"
        "<p><a href=\"/\">Back</a></p>"
        "</body>"
        "</html>";

    httpd_resp_set_type( req, "text/html; charset=utf-8" );
    return httpd_resp_send( req, response, HTTPD_RESP_USE_STRLEN );
}

static httpd_handle_t start_wifi_setup_webserver( void )
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Form handlers format floating-point values; retain stack headroom.
    config.stack_size = 8192;
    httpd_handle_t server = nullptr;

    ESP_LOGI( TAG, "Starting Wi-Fi setup web server" );

    esp_err_t err = httpd_start( &server, &config );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to start HTTP server: %s", esp_err_to_name( err ) );
        return nullptr;
    }

    static const httpd_uri_t root_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = wifi_setup_root_handler,
        .user_ctx = nullptr
    };
    static const httpd_uri_t connect_uri = {
        .uri      = "/connect",
        .method   = HTTP_POST,
        .handler  = wifi_connect_handler,
        .user_ctx = nullptr
    };
    static const httpd_uri_t station_uri = {
        .uri      = "/station",
        .method   = HTTP_GET,
        .handler  = station_get_handler,
        .user_ctx = nullptr
    };
    static const httpd_uri_t station_save_uri = {
        .uri      = "/station/save",
        .method   = HTTP_POST,
        .handler  = station_save_handler,
        .user_ctx = nullptr
    };

    err = httpd_register_uri_handler( server, &root_uri );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to register root URI: %s", esp_err_to_name( err ) );
        httpd_stop( server );
        return nullptr;
    }
    err = httpd_register_uri_handler( server, &connect_uri );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to register connect URI: %s", esp_err_to_name( err ) );
        httpd_stop( server );
        return nullptr;
    }
    err = httpd_register_uri_handler( server, &station_uri );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to register station URI: %s", esp_err_to_name( err ) );
        httpd_stop( server );
        return nullptr;
    }
    err = httpd_register_uri_handler( server, &station_save_uri );
    if ( err != ESP_OK ) {
        ESP_LOGE( TAG, "Failed to register station save URI: %s", esp_err_to_name( err ) );
        httpd_stop( server );
        return nullptr;
    }

    err = register_field_handlers(server);
    if (err != ESP_OK) { httpd_stop(server); return nullptr; }
    ESP_LOGI( TAG, "Wi-Fi setup web server started" );
    return server;
}

esp_err_t wifi_device_name(char* name, size_t size)
{
    if (!name || size < 16) return ESP_ERR_INVALID_ARG;
    name[0] = '\0';
    uint8_t mac[6] = {};
    const esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) return err;
    snprintf(name, size, "Tab5-HRO-%02X%02X%02X",
             static_cast<unsigned>(mac[3]), static_cast<unsigned>(mac[4]), static_cast<unsigned>(mac[5]));
    return ESP_OK;
}

void wifi_start_ap( const char *saved_ssid, const char *saved_password )
{
    ESP_LOGI( TAG, "Starting Wi-Fi SoftAP" );

    // TCP/IP stack
    ESP_ERROR_CHECK( esp_netif_init() );

    // Default event loop
    esp_err_t err = esp_event_loop_create_default();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK( err );
    }

    // SoftAP network interface
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // Wi-Fi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init( &cfg ) );
    wifi_config_t wifi_config = {};

    ESP_ERROR_CHECK( esp_event_handler_register( WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr ) );
    ESP_ERROR_CHECK( esp_event_handler_register( IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr ) );

    char ssid[32];
    ESP_ERROR_CHECK(wifi_device_name(ssid, sizeof(ssid)));
    strncpy( reinterpret_cast<char *>(wifi_config.ap.ssid), ssid, sizeof( wifi_config.ap.ssid ) - 1 );
    wifi_config.ap.ssid[sizeof( wifi_config.ap.ssid ) - 1] = '\0';
    wifi_config.ap.ssid_len = strlen( reinterpret_cast<char *>(wifi_config.ap.ssid) );

    // 初期テストなのでパスワードなし
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.max_connection = 4;
    ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_APSTA ) );
//    ESP_ERROR_CHECK( esp_wifi_set_mode( WIFI_MODE_AP ) );
    ESP_ERROR_CHECK( esp_wifi_set_config( WIFI_IF_AP, &wifi_config ) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_LOGI( TAG, "Wi-Fi SoftAP started: SSID=%s", ssid );
    console_printf(ConsoleLevel::Info, "Wi-Fi setup AP: %s", ssid);

    if (saved_ssid[0] != '\0') {
        wifi_connect_sta( saved_ssid, saved_password );
    }
    start_wifi_setup_webserver();
}

} // namespace hro::tab5::app
