#include <cairo/cairo.h>

#include "hro_config.h"
#include "hro_fft_config.h"
#include "hro_version.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdio>

namespace {

constexpr int WIDTH  = 1280;
constexpr int HEIGHT = 480;

constexpr uint16_t HRO_PNG_UDP_PORT = 50001;

constexpr uint32_t HRO_LIVE_MAGIC = 0x48524F31; // "HRO1"
constexpr uint16_t HRO_LIVE_VERSION = 1;

//constexpr std::size_t FFT_BINS = 601;   // 501 -> 601
constexpr std::size_t FFT_BINS = hro::FFT_BIN_COUNT;
constexpr std::size_t HRO_LIVE_PACKET_SIZE = 28 + FFT_BINS * sizeof(float);
constexpr int PNG_SECONDS = 20 * 60; // 1200

struct PngSecond
{
    bool valid = false;

    uint64_t sequence = 0;

    int64_t timestampMs = 0;

    float peakDb = 0.0f;
    int displayLevelDb = 0;

    std::array<float, FFT_BINS> fftDb{};
};

using PngBuffer =
    std::array<PngSecond, PNG_SECONDS>;

// ------------------------------------------------------------
// Plot geometry
// ------------------------------------------------------------

constexpr int SCROLL_LEFT  = 48;
constexpr int SCROLL_RIGHT = 1248;

constexpr int WATERFALL_TOP    = 125;
constexpr int WATERFALL_BOTTOM = 360;
constexpr int WATERFALL_HEIGHT =
    WATERFALL_BOTTOM - WATERFALL_TOP;

constexpr int LEVEL_TOP    = 370;
constexpr int LEVEL_H      = 80;
constexpr int LEVEL_BOTTOM =
    LEVEL_TOP + LEVEL_H;

constexpr double LEVEL_DB_MIN = -10.0;
constexpr double LEVEL_DB_MAX =  30.0;

static_assert(
    SCROLL_RIGHT - SCROLL_LEFT == PNG_SECONDS,
    "PNG plot width must equal PNG_SECONDS");

// ------------------------------------------------------------
// Big endian decode
// ------------------------------------------------------------

uint16_t readBe16(const uint8_t* p)
{
    return
        (static_cast<uint16_t>(p[0]) << 8) |
         static_cast<uint16_t>(p[1]);
}


uint32_t readBe32(const uint8_t* p)
{
    return
        (static_cast<uint32_t>(p[0]) << 24) |
        (static_cast<uint32_t>(p[1]) << 16) |
        (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}


uint64_t readBe64(const uint8_t* p)
{
    uint64_t value = 0;

    for (int i = 0; i < 8; ++i)
    {
        value =
            (value << 8) |
            static_cast<uint64_t>(p[i]);
    }

    return value;
}


float readBeFloat(const uint8_t* p)
{
    const uint32_t bits =
        readBe32(p);

    float value;

    std::memcpy(
        &value,
        &bits,
        sizeof(value));

    return value;
}


// ------------------------------------------------------------
// Time
// ------------------------------------------------------------

// 20 minute block:
//
// xx:00:00
// xx:20:00
// xx:40:00
//
// Returned value is Unix time in seconds.
//
int64_t getBlockStart(int64_t timestampMs)
{
    const int64_t seconds =
        timestampMs / 1000;

    return
        (seconds / PNG_SECONDS) *
        PNG_SECONDS;
}


std::string formatLocalTime(
    int64_t unixSeconds)
{
    const std::time_t t =
        static_cast<std::time_t>(
            unixSeconds);

    std::tm tm{};

    localtime_r(&t, &tm);

    char text[64];

    std::strftime(
        text,
        sizeof(text),
        "%Y/%m/%d %H:%M:%S",
        &tm);

    return text;
}

std::string formatIso8601Local(
    int64_t unixSeconds)
{
    const std::time_t t =
        static_cast<std::time_t>(unixSeconds);

    std::tm tm{};
    localtime_r(&t, &tm);

    char text[64];

    std::strftime(
        text,
        sizeof(text),
        "%Y-%m-%dT%H:%M:%S%z",
        &tm);

    std::string result(text);

    // +0900 -> +09:00
    if (result.size() >= 5)
    {
        result.insert(
            result.size() - 2,
            ":");
    }

    return result;
}

std::string makePngPath(
    const HroConfig& config,
    int64_t blockStart)
{
    const std::time_t t =
        static_cast<std::time_t>(blockStart);

    std::tm tm{};
    localtime_r(&t, &tm);

    char year[8];
    char month[16];
    char day[16];
    char timestamp[32];

    std::strftime(
        year,
        sizeof(year),
        "%Y",
        &tm);

    std::strftime(
        month,
        sizeof(month),
        "%Y%m",
        &tm);

    std::strftime(
        day,
        sizeof(day),
        "%Y%m%d",
        &tm);

    std::strftime(
        timestamp,
        sizeof(timestamp),
        "%Y%m%d%H%M",
        &tm);

    const std::filesystem::path directory =
        std::filesystem::path("/mnt/hro/png") /
        year /
        month /
        day;

    std::error_code ec;

    std::filesystem::create_directories(
        directory,
        ec);

    if (ec)
    {
        std::cerr
            << "ERROR: Failed to create PNG directory: "
            << directory
            << " : "
            << ec.message()
            << '\n';

        return {};
    }

    const std::string filename =
        config.screenshot_prefix +
        timestamp +
        ".png";

    return
        (directory / filename).string();
}

std::string makeJsonPath(
    const HroConfig& config,
    int64_t blockStart)
{
    const std::time_t t =
        static_cast<std::time_t>(blockStart);

    std::tm tm{};
    localtime_r(&t, &tm);

    char year[8];
    char month[16];
    char day[16];
    char timestamp[32];

    std::strftime(
        year,
        sizeof(year),
        "%Y",
        &tm);

    std::strftime(
        month,
        sizeof(month),
        "%Y%m",
        &tm);

    std::strftime(
        day,
        sizeof(day),
        "%Y%m%d",
        &tm);

    std::strftime(
        timestamp,
        sizeof(timestamp),
        "%Y%m%d%H%M",
        &tm);

    const std::filesystem::path directory =
        std::filesystem::path("/mnt/hro/png") /
        year /
        month /
        day;

    const std::string filename =
        config.screenshot_prefix +
        timestamp +
        ".json";

    return
        (directory / filename).string();
}

std::string escapeJson(
    const std::string& text)
{
    std::ostringstream out;

    for (const unsigned char c : text)
    {
        switch (c)
        {
        case '"':
            out << "\\\"";
            break;

        case '\\':
            out << "\\\\";
            break;

        case '\b':
            out << "\\b";
            break;

        case '\f':
            out << "\\f";
            break;

        case '\n':
            out << "\\n";
            break;

        case '\r':
            out << "\\r";
            break;

        case '\t':
            out << "\\t";
            break;

        default:
            if (c < 0x20)
            {
                out
                    << "\\u"
                    << std::hex
                    << std::setw(4)
                    << std::setfill('0')
                    << static_cast<int>(c)
                    << std::dec
                    << std::setfill(' ');
            }
            else
            {
                out << static_cast<char>(c);
            }

            break;
        }
    }

    return out.str();
}

bool writeJson(
    const HroConfig& config,
    const PngBuffer& buffer,
    int64_t blockStart)
{
    const std::string jsonPath =
        makeJsonPath(
            config,
            blockStart);

    if (jsonPath.empty())
    {
        return false;
    }

    int receivedCount = 0;

    bool haveSequence = false;
    uint64_t firstSequence = 0;
    uint64_t lastSequence = 0;

    for (const auto& s : buffer)
    {
        if (!s.valid)
        {
            continue;
        }

        ++receivedCount;

        if (!haveSequence)
        {
            firstSequence = s.sequence;
            haveSequence = true;
        }

        lastSequence = s.sequence;
    }

    const int missingCount =
        PNG_SECONDS - receivedCount;

    const std::string pngPath =
        makePngPath(
            config,
            blockStart);

    const std::string pngFile =
        std::filesystem::path(
            pngPath).filename().string();

    std::ofstream out(jsonPath);

    if (!out)
    {
        std::cerr
            << "ERROR: Failed to create JSON: "
            << jsonPath
            << '\n';

        return false;
    }

    out << std::fixed
        << std::setprecision(6);

    out
        << "{\n"
        << "  \"format_version\": \""
        << hro::DATA_FORMAT_VERSION << "\",\n"

        << "  \"software\": {\n"
        << "    \"name\": \""
        << hro::SOFTWARE_NAME << "\",\n"
        << "    \"version\": \""
        << hro::SOFTWARE_VERSION << "\"\n"
        << "  },\n"

        << "  \"station\": {\n"
        << "    \"id\": \""
        << escapeJson(config.screenshot_prefix) << "\",\n"
        << "    \"observer\": \""
        << escapeJson(config.observer) << "\",\n"
        << "    \"location\": \""
        << escapeJson(config.location) << "\",\n"
        << "    \"latitude\": "
        << config.latitude << ",\n"
        << "    \"longitude\": "
        << config.longitude << "\n"
        << "  },\n"

        << "  \"observation\": {\n"
        << "    \"start_time\": \""
        << formatIso8601Local(blockStart) << "\",\n"
        << "    \"end_time\": \""
        << formatIso8601Local(
               blockStart + PNG_SECONDS) << "\",\n"
        << "    \"duration_sec\": "
        << PNG_SECONDS << ",\n"
        << "    \"png_file\": \""
        << escapeJson(pngFile) << "\"\n"
        << "  },\n"

        << "  \"receiver\": {\n"
        << "    \"model\": \""
        << escapeJson(config.receiver) << "\",\n"
        << "    \"antenna\": \""
        << escapeJson(config.antenna) << "\",\n"
        << "    \"frequency_hz\": "
        << config.frequency_hz << ",\n"
        << "    \"gain_db\": "
        << (config.sdr_gain / 10.0) << "\n"
        << "  },\n"

        << "  \"fft\": {\n"
        << "    \"size\": 8192,\n"
        << "    \"window\": \"hann\",\n"
        << "    \"center_hz\": "
        << config.fft_center_hz << ",\n"
        << "    \"range_hz\": "
        << hro::FFT_RANGE_HZ << ",\n"
        << "    \"bins\": "
        << FFT_BINS << ",\n"
        << "    \"level_peak_range_hz\": "
        << config.level_peak_range_hz << "\n"
        << "  },\n"

        << "  \"display\": {\n"
        << "    \"waterfall_db_min\": -20.0,\n"
        << "    \"waterfall_db_max\": 30.0,\n"
        << "    \"level_db_min\": "
        << LEVEL_DB_MIN << ",\n"
        << "    \"level_db_max\": "
        << LEVEL_DB_MAX << "\n"
        << "  },\n"

        << "  \"data_quality\": {\n"
        << "    \"expected_seconds\": "
        << PNG_SECONDS << ",\n"
        << "    \"received_seconds\": "
        << receivedCount << ",\n"
        << "    \"missing_seconds\": "
        << missingCount << ",\n";

    if (haveSequence)
    {
        out
            << "    \"first_sequence\": "
            << firstSequence << ",\n"
            << "    \"last_sequence\": "
            << lastSequence << "\n";
    }
    else
    {
        out
            << "    \"first_sequence\": null,\n"
            << "    \"last_sequence\": null\n";
    }

    out
        << "  }\n"
        << "}\n";

    if (!out)
    {
        std::cerr
            << "ERROR: Failed while writing JSON: "
            << jsonPath
            << '\n';

        return false;
    }

    std::cout
        << "JSON written: "
        << jsonPath
        << '\n';

    return true;
}

// ------------------------------------------------------------
// Cairo helpers
// ------------------------------------------------------------

void setCyan(cairo_t* cr)
{
    cairo_set_source_rgb(
        cr,
        0x28 / 255.0,
        0xd7 / 255.0,
        0xf2 / 255.0);
}


void setWhite(cairo_t* cr)
{
    cairo_set_source_rgb(
        cr,
        0xf4 / 255.0,
        0xf7 / 255.0,
        0xf8 / 255.0);
}


void selectFont(
    cairo_t* cr,
    double size)
{
    cairo_select_font_face(
        cr,
        "Nunito Sans",
        CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_BOLD);

    cairo_set_font_size(
        cr,
        size);
}


void drawText(
    cairo_t* cr,
    const std::string& text,
    double x,
    double y)
{
    cairo_move_to(
        cr,
        x,
        y);

    cairo_show_text(
        cr,
        text.c_str());
}


void drawInfo(
    cairo_t* cr,
    const std::string& label,
    const std::string& value,
    double x,
    double labelY,
    double valueY,
    double valueSize)
{
    selectFont(
        cr,
        10.0);

    setCyan(cr);

    drawText(
        cr,
        label,
        x,
        labelY);

    selectFont(
        cr,
        valueSize);

    setWhite(cr);

    drawText(
        cr,
        value,
        x,
        valueY);
}


std::string decimalToDms(
    double value,
    char positiveDirection,
    char negativeDirection)
{
    const char direction =
        value >= 0.0
            ? positiveDirection
            : negativeDirection;

    value =
        std::fabs(value);

    const int degrees =
        static_cast<int>(
            std::floor(value));

    const double minutesRaw =
        (value - degrees) * 60.0;

    const int minutes =
        static_cast<int>(
            std::floor(minutesRaw));

    const double seconds =
        (minutesRaw - minutes) * 60.0;

    std::ostringstream ss;

    ss
        << degrees
        << "°"
        << std::setw(2)
        << std::setfill('0')
        << minutes
        << "'"
        << std::fixed
        << std::setprecision(3)
        << seconds
        << "\" "
        << direction;

    return ss.str();
}

// ------------------------------------------------------------
// Waterfall color
// Same mapping as monitor.html
// ------------------------------------------------------------

void setWaterfallColor(
    cairo_t* cr,
    double db)
{
    constexpr double DB_MIN = -20.0;
    constexpr double DB_MAX =  30.0;

    double t =
        (db - DB_MIN) /
        (DB_MAX - DB_MIN);

    t = std::clamp(t, 0.0, 1.0);

    double r = 0.0;
    double g = 0.0;
    double b = 0.0;

    // dark blue -> blue -> cyan -> yellow -> white
    if (t < 0.25)
    {
        const double u = t / 0.25;

        b = 30.0 + 180.0 * u;
    }
    else if (t < 0.50)
    {
        const double u =
            (t - 0.25) / 0.25;

        g = 220.0 * u;
        b = 220.0;
    }
    else if (t < 0.75)
    {
        const double u =
            (t - 0.50) / 0.25;

        r = 255.0 * u;
        g = 220.0;
        b = 220.0 * (1.0 - u);
    }
    else
    {
        const double u =
            (t - 0.75) / 0.25;

        r = 255.0;
        g = 220.0 + 35.0 * u;
        b = 255.0 * u;
    }

    cairo_set_source_rgb(
        cr,
        r / 255.0,
        g / 255.0,
        b / 255.0);
}


double levelDbToY(double db)
{
    db = std::clamp(
        db,
        LEVEL_DB_MIN,
        LEVEL_DB_MAX);

    const double normalized =
        (db - LEVEL_DB_MIN) /
        (LEVEL_DB_MAX - LEVEL_DB_MIN);

    return
        LEVEL_H - 1 -
        std::floor(
            normalized *
            (LEVEL_H - 1));
}

// ------------------------------------------------------------
// Waterfall
//
// 1200 seconds = 1200 pixels
// ------------------------------------------------------------

void drawWaterfall(
    cairo_t* cr,
    const PngBuffer& buffer)
{
    for (int secondIndex = 0;
         secondIndex < PNG_SECONDS;
         ++secondIndex)
    {
        const PngSecond& second =
            buffer[
                static_cast<std::size_t>(
                    secondIndex)];

        if (!second.valid)
            continue;

        const double x =
            SCROLL_LEFT + secondIndex;

        for (int y = 0;
             y < WATERFALL_HEIGHT;
             ++y)
        {
            // Same mapping as monitor.html:
            // top    = high frequency
            // bottom = low frequency
            //
            // fft[0]   = low frequency
            // fft[600] = high frequency

            const double ratio =
                1.0 -
                static_cast<double>(y) /
                (WATERFALL_HEIGHT - 1);

            const std::size_t bin =
                static_cast<std::size_t>(
                    std::lround(
                        ratio *
                        (FFT_BINS - 1)));

            setWaterfallColor(
                cr,
                second.fftDb[bin] + second.displayLevelDb);

            cairo_rectangle(
                cr,
                x,
                WATERFALL_TOP + y,
                1.0,
                1.0);

            cairo_fill(cr);
        }
    }
}

// ------------------------------------------------------------
// Peak level
//
// 1200 seconds = 1200 pixels
// ------------------------------------------------------------

void drawLevelGraph(
    cairo_t* cr,
    const PngBuffer& buffer)
{
    for (int secondIndex = 0;
         secondIndex < PNG_SECONDS;
         ++secondIndex)
    {
        const PngSecond& second =
            buffer[
                static_cast<std::size_t>(
                    secondIndex)];

        if (!second.valid)
            continue;

        const double x =
            SCROLL_LEFT + secondIndex;

        const double y =
            levelDbToY(
                second.peakDb);

        setWaterfallColor(
            cr,
            second.peakDb);

        cairo_rectangle(
            cr,
            x,
            LEVEL_TOP + y,
            1.0,
            LEVEL_H - y);

        cairo_fill(cr);
    }
}

void drawFrequencyAxis(
    cairo_t* cr,
    const HroConfig& config)
{
    selectFont(cr, 10.0);
    setCyan(cr);

    cairo_set_line_width(cr, 1.0);

    constexpr int TICK_HZ = 100;

    const int maxFrequency =
        config.fft_center_hz + hro::FFT_RANGE_HZ;

    const int minFrequency =
        config.fft_center_hz - hro::FFT_RANGE_HZ;

    // Highest 100 Hz tick inside the FFT display range
    const int firstTick =
        (maxFrequency / TICK_HZ) * TICK_HZ;

    for (int frequency = firstTick;
         frequency >= minFrequency;
         frequency -= TICK_HZ)
    {
        const double ratio =
            static_cast<double>(
                maxFrequency - frequency) /
            static_cast<double>(
                2 * hro::FFT_RANGE_HZ);

        const double y =
            WATERFALL_TOP +
            (WATERFALL_BOTTOM -
             WATERFALL_TOP) * ratio;

        // Left tick
        cairo_move_to(cr, SCROLL_LEFT - 5, y);
        cairo_line_to(cr, SCROLL_LEFT - 1, y);

        // Right tick
        cairo_move_to(cr, SCROLL_RIGHT + 1, y);
        cairo_line_to(cr, SCROLL_RIGHT + 5, y);

        cairo_stroke(cr);

        const std::string text =
            std::to_string(frequency);

        cairo_text_extents_t extents{};

        cairo_text_extents(
            cr,
            text.c_str(),
            &extents);

        drawText(
            cr,
            text,
            40 - extents.width,
            y + extents.height / 2.0);
    }
}

void drawLevelAxis(
    cairo_t* cr)
{
    constexpr std::array<int, 4> values =
        {20, 10, 0, -10};

    selectFont(cr, 10.0);
    setCyan(cr);

    cairo_set_line_width(cr, 1.0);

    for (const int db : values)
    {
        const double y =
            LEVEL_TOP +
            levelDbToY(db);

        // Left tick only
        cairo_move_to(cr, SCROLL_LEFT - 5, y);
        cairo_line_to(cr, SCROLL_LEFT - 1, y);
        cairo_stroke(cr);

        std::ostringstream text;

        if (db > 0)
            text << '+';

        text << db;

        cairo_text_extents_t extents{};

        cairo_text_extents(
            cr,
            text.str().c_str(),
            &extents);

        drawText(
            cr,
            text.str(),
            40 - extents.width,
            y + extents.height / 2.0);
    }
}

void drawTimeAxis(
    cairo_t* cr,
    int64_t blockStart)
{
    selectFont(cr, 10.0);
    setCyan(cr);

    cairo_set_line_width(cr, 1.0);

    // 20 minutes, tick every 2 minutes.
    for (int minute = 0;
         minute <= 20;
         minute += 2)
    {
        const int second =
            minute * 60;

        const double x =
            SCROLL_LEFT + second;

        // Tick at bottom of Waterfall
        cairo_move_to(
            cr,
            x,
            WATERFALL_TOP - 1);

        cairo_line_to(
            cr,
            x,
            WATERFALL_TOP - 5);

        cairo_stroke(cr);

        const std::time_t tickTime =
            static_cast<std::time_t>(
                blockStart + second);

        std::tm tm{};
        localtime_r(&tickTime, &tm);

        char text[16];

        std::strftime(
            text,
            sizeof(text),
            "%H:%M",
            &tm);

        cairo_text_extents_t extents{};

        cairo_text_extents(
            cr,
            text,
            &extents);

        double textX =
            x - extents.width / 2.0;

        // Keep first/last label inside image.
        if (minute == 0)
            textX = SCROLL_LEFT;
        else if (minute == 20)
            textX =
                SCROLL_RIGHT - extents.width;

        drawText(
            cr,
            text,
            textX,
            WATERFALL_TOP - 9);
    }
}

// ------------------------------------------------------------
// PNG test output
//
// For this step we still draw only the fixed information.
// Waterfall / Peak will be added next.
// ------------------------------------------------------------

bool writePng(
    const HroConfig& config,
    const PngBuffer& buffer,
    int64_t blockStart)
{
    const char* baseImage =
        "ui/assets/radio_meteor_observation_base_1280x480.png";

    const std::string outputImage =
        makePngPath(
            config,
            blockStart);

    if (outputImage.empty())
    {
        return false;
    }

    cairo_surface_t* base =
        cairo_image_surface_create_from_png(
            baseImage);

    if (cairo_surface_status(base) !=
        CAIRO_STATUS_SUCCESS)
    {
        std::cerr
            << "ERROR: Failed to load base image: "
            << cairo_status_to_string(
                   cairo_surface_status(base))
            << '\n';

        cairo_surface_destroy(base);

        return false;
    }

    cairo_surface_t* output =
        cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32,
            WIDTH,
            HEIGHT);

    cairo_t* cr =
        cairo_create(output);

    // Base image
    cairo_set_source_surface(
        cr,
        base,
        0.0,
        0.0);

    cairo_paint(cr);

    // --------------------------------------------------------
    // Observation data
    // --------------------------------------------------------

    drawWaterfall(cr, buffer);
    drawLevelGraph(cr,buffer);    

    drawFrequencyAxis(cr, config);
    drawLevelAxis(cr);
    drawTimeAxis(cr, blockStart);

    // --------------------------------------------------------
    // System information
    // --------------------------------------------------------

    selectFont(
        cr,
        10.0);

    cairo_set_source_rgb(
        cr,
        0x20 / 255.0,
        0xdf / 255.0,
        0xf3 / 255.0);


    // --------------------------------------------------------
    // Coordinates
    // --------------------------------------------------------

    const std::string longitude =
        decimalToDms(
            config.longitude,
            'E',
            'W');

    const std::string latitude =
        decimalToDms(
            config.latitude,
            'N',
            'S');


    // --------------------------------------------------------
    // Gain
    // --------------------------------------------------------

    std::ostringstream gainText;

    gainText
        << std::fixed
        << std::setprecision(1)
        << (config.sdr_gain / 10.0)
        << " dB";

    // --------------------------------------------------------
    // Receiving frequency
    // --------------------------------------------------------

    std::ostringstream frequencyText;

    frequencyText
        << std::fixed
        << std::setprecision(6)
        << static_cast<double>(
               config.frequency_hz) /
               1000000.0
        << " MHz";


    // --------------------------------------------------------
    // FFT
    // --------------------------------------------------------

    std::ostringstream fftText;

    fftText
        << config.fft_center_hz
        << " +/-"
        << hro::FFT_RANGE_HZ
        << " Hz";


    // --------------------------------------------------------
    // Header
    // --------------------------------------------------------

    drawInfo(
        cr,
        "OBSERVER",
        config.observer,
        485, 24, 43, 13);

    drawInfo(
        cr,
        "RECEIVING LOCATION",
        config.location,
        485, 57, 75, 13);

    drawInfo(
        cr,
        "LONGITUDE",
        longitude,
        785, 24, 43, 11);

    drawInfo(
        cr,
        "LATITUDE",
        latitude,
        785, 57, 75, 11);

    drawInfo(
        cr,
        "RECEIVER",
        config.receiver,
        905, 24, 43, 12);

    drawInfo(
        cr,
        "ANTENNA",
        config.antenna,
        905, 57, 75, 12);

    drawInfo(
        cr,
        "GAIN",
        gainText.str(),
        1050, 24, 43, 12);

    drawInfo(
        cr,
        "RECEIVING FREQUENCY",
        frequencyText.str(),
        1120, 24, 43, 12);

    drawInfo(
        cr,
        "FFT CENTER / RANGE",
        fftText.str(),
        1120, 57, 75, 12);


    // Temporary block information.
    // This is useful while testing the 20-minute boundary.
    selectFont(
        cr,
        10.0);

    setWhite(cr);

    // --------------------------------------------------------
    // Observation start time / PNG filename
    // --------------------------------------------------------

    const std::time_t blockTime =
        static_cast<std::time_t>(
            blockStart);

    std::tm blockTm{};
    localtime_r(&blockTime, &blockTm);

    char observationTime[64];

    std::strftime(
        observationTime,
        sizeof(observationTime),
        "%Y/%m/%d %H:%M",
        &blockTm);

    const std::string pngFilename =
        std::filesystem::path(
            outputImage).filename().string();

    selectFont(
        cr,
        20.0);

    setWhite(cr);

    drawText(
        cr,
        std::string(observationTime) + "(JST)",
        40,
        472);

    drawText(
        cr,
        pngFilename,
        303,
        472);    

    const cairo_status_t status =
        cairo_surface_write_to_png(
            output,
            outputImage.c_str());

    cairo_destroy(cr);
    cairo_surface_destroy(output);
    cairo_surface_destroy(base);

    if (status !=
        CAIRO_STATUS_SUCCESS)
    {
        std::cerr
            << "ERROR: Failed to write PNG: "
            << cairo_status_to_string(status)
            << '\n';

        return false;
    }

    std::cout
        << "PNG written: "
        << outputImage
        << '\n';

    return true;
}

} // namespace


int main()
{
    // --------------------------------------------------------
    // Configuration
    // --------------------------------------------------------

    HroConfig config;

    if (!config.load(
            "/etc/hro/config.ini"))
    {
        std::cerr
            << "WARNING: Failed to load "
            << "/etc/hro/config.ini; "
            << "using defaults\n";
    }


    // --------------------------------------------------------
    // UDP socket
    // --------------------------------------------------------

    const int udpSocket =
        ::socket(
            AF_INET,
            SOCK_DGRAM,
            0);

    if (udpSocket < 0)
    {
        std::cerr
            << "ERROR: Failed to create "
            << "PNG UDP socket\n";

        return 1;
    }


    sockaddr_in address{};

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        htonl(INADDR_ANY);

    address.sin_port =
        htons(HRO_PNG_UDP_PORT);


    if (::bind(
            udpSocket,
            reinterpret_cast<
                const sockaddr*>(
                    &address),
            sizeof(address)) < 0)
    {
        std::cerr
            << "ERROR: Failed to bind "
            << "PNG UDP port "
            << HRO_PNG_UDP_PORT
            << '\n';

        ::close(udpSocket);

        return 1;
    }


    std::cout
        << "HRO PNG receiver\n"
        << "UDP port : "
        << HRO_PNG_UDP_PORT
        << '\n'
        << "Block    : "
        << PNG_SECONDS
        << " seconds\n"
        << "Waiting for HRO data...\n";


    // --------------------------------------------------------
    // 20-minute buffer
    // --------------------------------------------------------

    PngBuffer buffer{};

    bool haveBlock = false;

    int64_t currentBlockStart = 0;

    std::array<
        uint8_t,
        HRO_LIVE_PACKET_SIZE>
        packet{};


    // --------------------------------------------------------
    // Receive forever
    // --------------------------------------------------------

    while (true)
    {
        const ssize_t received =
            ::recvfrom(
                udpSocket,
                packet.data(),
                packet.size(),
                0,
                nullptr,
                nullptr);


        if (received < 0)
        {
            std::cerr
                << "WARNING: UDP receive failed\n";

            continue;
        }


        if (received !=
            static_cast<ssize_t>(
                HRO_LIVE_PACKET_SIZE))
        {
            std::cerr
                << "WARNING: Invalid UDP packet size: "
                << received
                << '\n';

            continue;
        }


        // ----------------------------------------------------
        // Header decode
        // ----------------------------------------------------

        const uint32_t magic =
            readBe32(
                packet.data() + 0);

        const uint16_t version =
            readBe16(
                packet.data() + 4);

        const uint16_t binCount =
            readBe16(
                packet.data() + 6);

        const uint64_t sequence =
            readBe64(
                packet.data() + 8);

        const int64_t timestampMs =
            static_cast<int64_t>(
                readBe64(
                    packet.data() + 16));

        const float peakDb =
            readBeFloat(
                packet.data() + 24);


        if (magic != HRO_LIVE_MAGIC ||
            version != HRO_LIVE_VERSION ||
            binCount != FFT_BINS)
        {
            std::cerr
                << "WARNING: Invalid HRO LIVE packet\n";

            continue;
        }


        // ----------------------------------------------------
        // Determine 20-minute block
        // ----------------------------------------------------

        const int64_t blockStart =
            getBlockStart(
                timestampMs);


        // First packet after startup.
        if (!haveBlock)
        {
            currentBlockStart =
                blockStart;

            haveBlock = true;

            std::cout
                << "Started block: "
                << formatLocalTime(
                       currentBlockStart)
                << '\n';
        }


        // ----------------------------------------------------
        // New 20-minute block
        // ----------------------------------------------------

        if (blockStart !=
            currentBlockStart)
        {
            int receivedCount = 0;

            for (const auto& s : buffer) {
                if (s.valid) {
                    ++receivedCount;
                }
            }

            const int missingCount =
                PNG_SECONDS - receivedCount;

            std::cout
                << "Block statistics: received="
                << receivedCount << "/" << PNG_SECONDS
                << "  missing=" << missingCount
                << std::endl;

            std::cout
                << "Completed block: "
                << formatLocalTime(
                       currentBlockStart)
                << '\n';

            // For this test step, write the previous block.
            //
            // Later this will be moved away from the receive
            // path so PNG compression cannot delay UDP receive.
            const auto pngStart = std::chrono::steady_clock::now();

            const bool pngSaved = writePng(config, buffer, currentBlockStart);
            const std::string pngPath = makePngPath(config, currentBlockStart);
            std::ofstream status("/mnt/hro/png/.latest-png.json.tmp");
            status << "{\"timestamp_ms\":" << timestampMs
                   << ",\"saved\":" << (pngSaved ? "true" : "false")
                   << ",\"received_seconds\":" << receivedCount
                   << ",\"missing_seconds\":" << missingCount
                   << ",\"filename\":\"" << escapeJson(std::filesystem::path(pngPath).filename().string()) << "\"}";
            status.flush();
            if (status.good()) {
                status.close();
                std::rename("/mnt/hro/png/.latest-png.json.tmp", "/mnt/hro/png/.latest-png.json");
            }

            const auto pngEnd = std::chrono::steady_clock::now();

            const auto pngMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    pngEnd - pngStart
                ).count();

            std::cout
                << "PNG generation time: "
                << pngMs
                << " ms\n";

            writeJson( config, buffer, currentBlockStart);

            // Clear for next block.
            buffer =
                PngBuffer{};

            currentBlockStart =
                blockStart;


            std::cout
                << "Started block: "
                << formatLocalTime(
                       currentBlockStart)
                << '\n';
        }


        // ----------------------------------------------------
        // Position within current 20-minute block
        // ----------------------------------------------------

        const int64_t timestampSeconds =
            timestampMs / 1000;

        const int secondIndex =
            static_cast<int>(
                timestampSeconds -
                currentBlockStart);


        if (secondIndex < 0 ||
            secondIndex >= PNG_SECONDS)
        {
            std::cerr
                << "WARNING: Invalid second index: "
                << secondIndex
                << '\n';

            continue;
        }


        // ----------------------------------------------------
        // Store this second
        // ----------------------------------------------------

        PngSecond& second =
            buffer[
                static_cast<std::size_t>(
                    secondIndex)];

        // Read the small local config once per incoming second. Reload appearance only;
        // observation settings still require the normal coordinated restart.
        HroConfig appearance;
        if (appearance.load("/etc/hro/config.ini")) config.display_level_db = appearance.display_level_db;
        second.displayLevelDb = config.display_level_db;
        second.valid =
            true;

        second.sequence =
            sequence;

        second.timestampMs =
            timestampMs;

        second.peakDb =
            peakDb;


        for (std::size_t i = 0;
             i < FFT_BINS;
             ++i)
        {
            second.fftDb[i] =
                readBeFloat(
                    packet.data() +
                    28 +
                    i * sizeof(float));
        }


        // Print every 60 seconds only.
        // Avoid flooding the terminal.
        if ((secondIndex % 60) == 0)
        {
            std::cout
                << "Block "
                << formatLocalTime(
                       currentBlockStart)
                << "  second="
                << secondIndex
                << "  seq="
                << sequence
                << "  peak="
                << peakDb
                << " dB\n";
        }
    }


    ::close(udpSocket);

    return 0;
}