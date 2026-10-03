#include "tab5_runtime.hpp"
#include "tab5_time.hpp"

namespace hro::tab5::app {
static bool g_ntp_started = false;
static void time_sync_notification_cb( struct timeval *tv );

static void time_sync_notification_cb( struct timeval *tv )
{
    ESP_LOGI( TAG, "NTP time synchronized" );

    time_t now;
    time( &now );

    struct tm utc_time;
    gmtime_r( &now, &utc_time );

    char buf[64];
    strftime( buf, sizeof( buf ), "%Y-%m-%d %H:%M:%S UTC", &utc_time );
    ESP_LOGI( TAG, "UTC time: %s", buf );

    // NTPのUTC時刻をTab5 RTCへ保存
    if ( M5.Rtc.isEnabled() ) {
        m5::rtc_datetime_t rtc_dt = {
            {
                static_cast<int16_t>( utc_time.tm_year + 1900 ),
                static_cast<int8_t>( utc_time.tm_mon + 1 ),
                static_cast<int8_t>( utc_time.tm_mday )
            },
            {
                static_cast<int8_t>( utc_time.tm_hour ),
                static_cast<int8_t>( utc_time.tm_min ),
                static_cast<int8_t>( utc_time.tm_sec )
            }
        };
        g_ntp_synced.store( true, std::memory_order_relaxed );
        M5.Rtc.setDateTime( rtc_dt );
        ESP_LOGI( TAG, "RTC synchronized from NTP (UTC)" );
        auto verify = M5.Rtc.getDateTime();
        ESP_LOGI(
            TAG,
            "RTC readback: %04d-%02d-%02d %02d:%02d:%02d UTC",
            verify.date.year,
            verify.date.month,
            verify.date.date,
            verify.time.hours,
            verify.time.minutes,
            verify.time.seconds );
    } else {
        ESP_LOGE( TAG, "RTC is not available" );
    }
}

void start_ntp( void )
{
    if ( g_ntp_started ) {
        return;
    }

    ESP_LOGI( TAG, "Starting NTP" );

    esp_sntp_setoperatingmode( SNTP_OPMODE_POLL );
    esp_sntp_setservername( 0, "pool.ntp.org" );
    esp_sntp_set_time_sync_notification_cb( time_sync_notification_cb );

    // 1時間ごとに再同期
    esp_sntp_set_sync_interval( 60 * 60 * 1000UL );
    esp_sntp_init();
    g_ntp_started = true;
}

bool restore_system_time_from_rtc( void )
{
    if ( !M5.Rtc.isEnabled() ) {
        ESP_LOGW( TAG, "RTC is not available" );
        return false;
    }

    auto rtc_dt = M5.Rtc.getDateTime();
    ESP_LOGI(
        TAG,
        "RTC at boot: %04d-%02d-%02d "
        "%02d:%02d:%02d UTC",
        rtc_dt.date.year,
        rtc_dt.date.month,
        rtc_dt.date.date,
        rtc_dt.time.hours,
        rtc_dt.time.minutes,
        rtc_dt.time.seconds );

    // RTC値の最低限の妥当性チェック
    if ( rtc_dt.date.year < 2024 ||
        rtc_dt.date.year > 2099 ||
        rtc_dt.date.month < 1 ||
        rtc_dt.date.month > 12 ||
        rtc_dt.date.date < 1 ||
        rtc_dt.date.date > 31 ||
        rtc_dt.time.hours < 0 ||
        rtc_dt.time.hours > 23 ||
        rtc_dt.time.minutes < 0 ||
        rtc_dt.time.minutes > 59 ||
        rtc_dt.time.seconds < 0 ||
        rtc_dt.time.seconds > 59 ) {

        ESP_LOGW( TAG, "RTC time is invalid" );
        return false;
    }

    struct tm t = {};

    t.tm_year = rtc_dt.date.year - 1900;
    t.tm_mon  = rtc_dt.date.month - 1;
    t.tm_mday = rtc_dt.date.date;

    t.tm_hour = rtc_dt.time.hours;
    t.tm_min  = rtc_dt.time.minutes;
    t.tm_sec  = rtc_dt.time.seconds;

    // RTCはUTCで保存しているのでUTCとして変換
    // mktime() にUTCとして解釈させる。
    setenv( "TZ", "UTC0", 1 );
    tzset();
    time_t epoch = mktime( &t );

    if ( epoch < 0 ) {
        ESP_LOGW( TAG, "Failed to convert RTC time" );
        return false;
    }

    struct timeval tv = {};
    tv.tv_sec  = epoch;
    tv.tv_usec = 0;

    if ( settimeofday( &tv, nullptr ) != 0 ) {
        ESP_LOGE( TAG, "settimeofday failed" );
        return false;
    }

    ESP_LOGI( TAG, "System clock restored from RTC" );
    return true;
}

} // namespace hro::tab5::app
