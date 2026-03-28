/**
 * @file St7789SupervisorDriver.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h"

#include <Arduino.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Modules/SupervisorHMIModule/Drivers/FlowIoLogoBitmap.h"

namespace {
constexpr uint16_t rgb565_(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
}

static constexpr uint16_t kColorBg = rgb565_(239, 245, 252);
static constexpr uint16_t kColorHeader = rgb565_(255, 255, 255);
static constexpr uint16_t kColorText = rgb565_(17, 24, 39);
static constexpr uint16_t kColorMuted = rgb565_(108, 122, 141);
static constexpr uint16_t kColorDivider = rgb565_(217, 227, 239);
static constexpr uint16_t kColorWifiOff = rgb565_(200, 211, 226);
static constexpr uint16_t kColorOn = rgb565_(44, 204, 116);
static constexpr uint16_t kColorOff = rgb565_(148, 163, 184);
static constexpr uint16_t kColorAlarmAct = rgb565_(224, 72, 72);
static constexpr uint16_t kColorAlarmAck = rgb565_(240, 178, 85);
static constexpr uint16_t kColorGaugeOk = rgb565_(47, 158, 104);
static constexpr uint16_t kColorGaugeCardBg = rgb565_(255, 255, 255);
static constexpr uint16_t kColorGaugeTrack = rgb565_(222, 231, 242);
static constexpr uint16_t kColorBadgeDark = rgb565_(11, 18, 32);
static constexpr uint16_t kColorBadgeDarkOff = rgb565_(71, 85, 105);
static constexpr uint16_t kColorStatusGreen = rgb565_(48, 163, 104);
static constexpr uint16_t kColorStatusOrange = rgb565_(246, 186, 74);
static constexpr uint16_t kColorStatusRed = rgb565_(213, 75, 111);
static constexpr uint16_t kColorCheckFill = rgb565_(176, 239, 143);
static constexpr uint16_t kColorCheckMark = rgb565_(0, 144, 69);
static constexpr uint16_t kColorSystemFill = rgb565_(236, 252, 240);
static constexpr uint16_t kColorSystemBorder = rgb565_(197, 238, 208);
static constexpr uint16_t kColorSystemText = rgb565_(36, 156, 88);
static constexpr uint16_t kColorCardBorder = rgb565_(217, 227, 239);
static constexpr uint16_t kColorValue = rgb565_(14, 23, 43);
static constexpr uint16_t kColorWater = rgb565_(67, 131, 238);
static constexpr uint16_t kColorAir = rgb565_(27, 184, 219);
static constexpr uint16_t kColorPh = rgb565_(34, 197, 94);
static constexpr uint16_t kColorOrp = rgb565_(132, 82, 236);

static constexpr int16_t kHeaderH = 48;
static constexpr int16_t kSidePad = 8;
static constexpr int16_t kStatusPillW = 54;
static constexpr int16_t kStatusPillH = 20;
static constexpr int16_t kAlarmPillW = 54;
static constexpr int16_t kAlarmPillH = 20;
static constexpr int16_t kAlarmSummaryH = 28;
static constexpr uint8_t kMaxWifiBars = 5;
static constexpr uint8_t kRowCount = 7;
static constexpr uint32_t kAlarmPageRotateMs = 10000U;

enum class SupervisorPage : uint8_t {
    Measures = 1,
};

struct AlarmRowDef {
    const char* label;
};

struct GaugeBand {
    float from;
    float to;
    uint16_t color;
};

static constexpr AlarmRowDef kAlarmRows[kSupervisorAlarmSlotCount] = {
    {"Pression basse"},
    {"Pression haute"},
    {"Niv. bidon pH"},
    {"Niv. bidon chlore"},
};

static constexpr GaugeBand kPhGaugeBands[] = {
    {6.4f, 6.8f, kColorAlarmAct},
    {6.8f, 7.0f, kColorAlarmAck},
    {7.0f, 7.6f, kColorGaugeOk},
    {7.6f, 7.8f, kColorAlarmAck},
    {7.8f, 8.4f, kColorAlarmAct},
};

static constexpr GaugeBand kOrpGaugeBands[] = {
    {350.0f, 500.0f, kColorAlarmAct},
    {500.0f, 620.0f, kColorAlarmAck},
    {620.0f, 760.0f, kColorGaugeOk},
    {760.0f, 820.0f, kColorAlarmAck},
    {820.0f, 900.0f, kColorAlarmAct},
};

static constexpr GaugeBand kWaterTempGaugeBands[] = {
    {0.0f, 8.0f, kColorAlarmAct},
    {8.0f, 14.0f, kColorAlarmAck},
    {14.0f, 30.0f, kColorGaugeOk},
    {30.0f, 34.0f, kColorAlarmAck},
    {34.0f, 40.0f, kColorAlarmAct},
};

static constexpr GaugeBand kAirTempGaugeBands[] = {
    {-10.0f, 0.0f, kColorAlarmAct},
    {0.0f, 8.0f, kColorAlarmAck},
    {8.0f, 28.0f, kColorGaugeOk},
    {28.0f, 35.0f, kColorAlarmAck},
    {35.0f, 45.0f, kColorAlarmAct},
};

uint16_t panelColor_(bool swapBytes, uint16_t c)
{
    if (!swapBytes) return c;
    return (uint16_t)((c << 8) | (c >> 8));
}

int clampi_(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float clampf_(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

uint8_t pctFromRssi_(int32_t dbm)
{
    const int v = clampi_((int)dbm, -100, -40);
    return (uint8_t)(((v + 100) * 100) / 60);
}

uint8_t barsFromPct_(uint8_t pct)
{
    return (uint8_t)((pct + 19U) / 20U);
}

int16_t textWidth_(const char* txt, uint8_t textSize)
{
    if (!txt) return 0;
    return (int16_t)(strlen(txt) * 6U * textSize);
}

void setDefaultFont_(SupervisorSt7789& d, bool swapBytes, uint16_t fg, uint16_t bg, uint8_t size)
{
    d.setFont(nullptr);
    d.setTextSize(size);
    d.setTextColor(panelColor_(swapBytes, fg), panelColor_(swapBytes, bg));
}

void setGfxFont_(SupervisorSt7789& d, bool swapBytes, const GFXfont* font, uint16_t fg, uint16_t bg)
{
    d.setFont(font);
    d.setTextSize(1);
    d.setTextColor(panelColor_(swapBytes, fg), panelColor_(swapBytes, bg));
}

void textBounds_(SupervisorSt7789& d,
                 const char* txt,
                 int16_t x,
                 int16_t y,
                 int16_t& x1,
                 int16_t& y1,
                 uint16_t& w,
                 uint16_t& h)
{
    if (!txt) txt = "";
    d.getTextBounds(txt, x, y, &x1, &y1, &w, &h);
}

int16_t gfxTextWidth_(SupervisorSt7789& d, const char* txt)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    textBounds_(d, txt, 0, 0, x1, y1, w, h);
    return (int16_t)w;
}

int16_t gfxBaselineCenteredInBox_(SupervisorSt7789& d, const char* txt, int16_t boxY, int16_t boxH)
{
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    textBounds_(d, txt, 0, 0, x1, y1, w, h);
    return (int16_t)(boxY + ((boxH - (int16_t)h) / 2) - y1);
}

void drawGfxText_(SupervisorSt7789& d,
                  bool swapBytes,
                  const GFXfont* font,
                  uint16_t fg,
                  uint16_t bg,
                  int16_t x,
                  int16_t baselineY,
                  const char* txt)
{
    setGfxFont_(d, swapBytes, font, fg, bg);
    d.setCursor(x, baselineY);
    d.print(txt ? txt : "");
}

void drawGfxTextCenteredY_(SupervisorSt7789& d,
                           bool swapBytes,
                           const GFXfont* font,
                           uint16_t fg,
                           uint16_t bg,
                           int16_t x,
                           int16_t boxY,
                           int16_t boxH,
                           const char* txt)
{
    setGfxFont_(d, swapBytes, font, fg, bg);
    d.setCursor(x, gfxBaselineCenteredInBox_(d, txt, boxY, boxH));
    d.print(txt ? txt : "");
}

void drawDashedHLine_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, int16_t w, uint16_t color)
{
    for (int16_t dx = 0; dx < w; dx += 6) {
        d.drawFastHLine((int16_t)(x + dx), y, 4, panelColor_(swapBytes, color));
    }
}

void drawWifiBars_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, uint8_t bars)
{
    static constexpr uint16_t kWifiColors[kMaxWifiBars] = {
        rgb565_(92, 147, 255),
        rgb565_(78, 135, 252),
        rgb565_(62, 122, 247),
        rgb565_(40, 201, 171),
        rgb565_(30, 181, 136),
    };
    static constexpr int16_t kBarW = 7;
    static constexpr int16_t kStep = 10;
    static constexpr int16_t kUnitH = 5;

    for (uint8_t i = 1; i <= kMaxWifiBars; ++i) {
        const int16_t h = (int16_t)(i * kUnitH);
        const int16_t yi = (int16_t)(y + (kMaxWifiBars * kUnitH) - h);
        const uint16_t color = (i <= bars) ? kWifiColors[i - 1U] : kColorWifiOff;
        d.fillRoundRect((int16_t)(x + (int16_t)((i - 1) * kStep)),
                        yi,
                        kBarW,
                        h,
                        2,
                        panelColor_(swapBytes, color));
    }
}

void normalizeAlarmLabel_(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    if (!in) in = "";

    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && (j + 1) < outLen; ++i) {
        const char c = in[i];
        out[j++] = (c == '_' || c == '-') ? ' ' : c;
    }
    out[j] = '\0';
}

void drawStatusPill_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, bool on)
{
    const uint16_t fill = on ? kColorOn : kColorOff;
    const char* label = on ? "ON" : "OFF";

    d.fillRoundRect(x, y, kStatusPillW, kStatusPillH, 4, panelColor_(swapBytes, fill));
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, kColorText, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((kStatusPillW - tw) / 2)), gfxBaselineCenteredInBox_(d, label, y, kStatusPillH));
    d.print(label);
}

void drawBadge_(SupervisorSt7789& d,
                bool swapBytes,
                int16_t x,
                int16_t y,
                int16_t w,
                int16_t h,
                uint16_t fill,
                uint16_t stroke,
                uint16_t text,
                const char* label)
{
    d.fillRoundRect(x, y, w, h, (int16_t)(h / 2), panelColor_(swapBytes, fill));
    if (stroke != fill) {
        d.drawRoundRect(x, y, w, h, (int16_t)(h / 2), panelColor_(swapBytes, stroke));
    }
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, text, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((w - tw) / 2)),
                (int16_t)(gfxBaselineCenteredInBox_(d, label, y, h) + 1));
    d.print(label);
}

void drawBootLogo_(SupervisorSt7789& d, bool swapBytes)
{
    const int16_t w = d.width();
    const int16_t h = d.height();
    d.fillScreen(panelColor_(swapBytes, rgb565_(255, 255, 255)));
    const int16_t x = (int16_t)((w - (int16_t)kFlowIoLogoWidth) / 2);
    const int16_t y = (int16_t)((h - (int16_t)kFlowIoLogoHeight) / 2);
    d.drawRGBBitmap(x, y, kFlowIoLogoBitmap, kFlowIoLogoWidth, kFlowIoLogoHeight);
}

void drawLogoWordmark_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, int16_t targetW, int16_t targetH)
{
    // Tight crop of the visible "Flow.IO" wordmark inside the splash logo.
    // This keeps the hero title aligned by its visible left edge instead of the
    // transparent padding carried by the original source bitmap.
    static constexpr int16_t kSrcX = 69;
    static constexpr int16_t kSrcY = 117;
    static constexpr int16_t kSrcW = 119;
    static constexpr int16_t kSrcH = 27;

    for (int16_t dy = 0; dy < targetH; ++dy) {
        const int16_t sy = (int16_t)(kSrcY + ((dy * kSrcH) / targetH));
        for (int16_t dx = 0; dx < targetW; ++dx) {
            const int16_t sx = (int16_t)(kSrcX + ((dx * kSrcW) / targetW));
            const uint32_t idx = (uint32_t)sy * (uint32_t)kFlowIoLogoWidth + (uint32_t)sx;
            const uint16_t c = pgm_read_word(&kFlowIoLogoBitmap[idx]);
            if (c != 0xFFFFU) {
                d.drawPixel((int16_t)(x + dx), (int16_t)(y + dy), panelColor_(swapBytes, c));
            }
        }
    }
}

void drawStaticLayout_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h, SupervisorPage page)
{
    (void)page;
    (void)h;
    d.fillScreen(panelColor_(swapBytes, kColorBg));
    d.fillRect(0, 0, w, kHeaderH, panelColor_(swapBytes, kColorHeader));
    d.drawFastHLine(0, (int16_t)(kHeaderH - 1), w, panelColor_(swapBytes, kColorDivider));
}

void drawHeaderWifi_(SupervisorSt7789& d, bool swapBytes, bool hasRssi, int32_t rssiDbm)
{
    d.fillRect(0, 0, 74, kHeaderH, panelColor_(swapBytes, kColorHeader));
    const uint8_t wifiBars = hasRssi ? barsFromPct_(pctFromRssi_(rssiDbm)) : 0U;
    drawWifiBars_(d, swapBytes, 14, 10, wifiBars);
}

void drawHeaderTime_(SupervisorSt7789& d, bool swapBytes, int16_t w, const char* timeTxt)
{
    static constexpr int16_t kTimeAreaW = 116;
    d.fillRect((int16_t)(w - kTimeAreaW), 0, kTimeAreaW, kHeaderH, panelColor_(swapBytes, kColorHeader));
    setGfxFont_(d, swapBytes, &FreeSans18pt7b, kColorText, kColorHeader);
    const int16_t tw = gfxTextWidth_(d, timeTxt ? timeTxt : "--:--");
    d.setCursor((int16_t)(w - tw - 12), 34);
    d.print(timeTxt ? timeTxt : "--:--");
}

void drawHeaderMqtt_(SupervisorSt7789& d, bool swapBytes, int16_t w, const char* timeTxt, bool mqttReady)
{
    (void)timeTxt;
    const int16_t pillW = 88;
    const int16_t pillH = 28;
    const int16_t pillX = (int16_t)((w - pillW) / 2);
    const int16_t clearX = (int16_t)(pillX - 8);
    const int16_t clearW = (int16_t)(pillW + 16);
    d.fillRect(clearX, 0, clearW, kHeaderH, panelColor_(swapBytes, kColorHeader));
    drawBadge_(d,
               swapBytes,
               pillX,
               10,
               pillW,
               pillH,
               mqttReady ? kColorStatusGreen : kColorStatusRed,
               mqttReady ? kColorStatusGreen : kColorStatusRed,
               rgb565_(255, 255, 255),
               "MQTT");
}

uint8_t systemState_(const SupervisorHmiViewModel& vm)
{
    if (!vm.flowLinkOk) return 0U;
    if (!vm.flowMqttReady) return 1U;
    return 2U;
}

uint16_t systemStateColor_(uint8_t state)
{
    if (state >= 2U) return kColorStatusGreen;
    if (state == 1U) return kColorStatusOrange;
    return kColorStatusRed;
}

void drawSystemStatus_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t x,
                       int16_t y,
                       int16_t w,
                       uint8_t state)
{
    d.fillRect(x, y, w, 24, panelColor_(swapBytes, kColorGaugeCardBg));
    if (state >= 2U) {
        const int16_t cx = (int16_t)(x + (w / 2));
        const int16_t cy = (int16_t)(y + 12);
        d.fillCircle(cx, cy, 10, panelColor_(swapBytes, kColorCheckFill));
        for (int8_t off = -1; off <= 1; ++off) {
            d.drawLine((int16_t)(cx - 5), (int16_t)(cy + off), (int16_t)(cx - 1), (int16_t)(cy + 4 + off), panelColor_(swapBytes, kColorCheckMark));
            d.drawLine((int16_t)(cx - 1), (int16_t)(cy + 4 + off), (int16_t)(cx + 6), (int16_t)(cy - 4 + off), panelColor_(swapBytes, kColorCheckMark));
        }
        return;
    }

    const char* label = (state == 1U) ? "MQTT off" : "Flow indispo";
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, systemStateColor_(state), kColorGaugeCardBg);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((w - tw) / 2)), (int16_t)(gfxBaselineCenteredInBox_(d, label, y, 24) + 1));
    d.print(label);
}

void drawCardLabel_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, uint16_t accent, const char* label)
{
    d.fillRoundRect(x, y, 30, 9, 4, panelColor_(swapBytes, accent));
    drawGfxText_(d, swapBytes, &FreeSans9pt7b, kColorMuted, kColorGaugeCardBg, (int16_t)(x + 40), (int16_t)(y + 10), label);
}

void drawRow_(SupervisorSt7789& d, bool swapBytes, int16_t w, uint8_t rowIndex, const char* label, bool on)
{
    const int16_t bodyTop = kHeaderH + 4;
    const int16_t rowH = (int16_t)((d.height() - bodyTop) / kRowCount);
    const int16_t y = (int16_t)(bodyTop + ((int16_t)rowIndex * rowH));
    const int16_t pillX = (int16_t)(w - kSidePad - kStatusPillW);
    const int16_t pillY = (int16_t)(y + ((rowH - kStatusPillH) / 2));

    d.fillRect(0, y, w, (int16_t)(rowH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorBg, kSidePad, y, rowH, label);
    drawStatusPill_(d, swapBytes, pillX, pillY, on);
}

const char* alarmStateLabel_(SupervisorAlarmState state)
{
    switch (state) {
        case SupervisorAlarmState::Active: return "ACT";
        case SupervisorAlarmState::Acked: return "ACK";
        case SupervisorAlarmState::Clear:
        default:
            return "CLR";
    }
}

uint16_t alarmStateColor_(SupervisorAlarmState state)
{
    switch (state) {
        case SupervisorAlarmState::Active: return kColorAlarmAct;
        case SupervisorAlarmState::Acked: return kColorAlarmAck;
        case SupervisorAlarmState::Clear:
        default:
            return kColorOff;
    }
}

void drawAlarmStatePill_(SupervisorSt7789& d, bool swapBytes, int16_t x, int16_t y, SupervisorAlarmState state)
{
    const uint16_t fill = alarmStateColor_(state);
    const char* label = alarmStateLabel_(state);

    d.fillRoundRect(x, y, kAlarmPillW, kAlarmPillH, 4, panelColor_(swapBytes, fill));
    setGfxFont_(d, swapBytes, &FreeSansBold9pt7b, kColorText, fill);
    const int16_t tw = gfxTextWidth_(d, label);
    d.setCursor((int16_t)(x + ((kAlarmPillW - tw) / 2)), gfxBaselineCenteredInBox_(d, label, y, kAlarmPillH));
    d.print(label);
}

void drawAlarmSummary_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t w,
                       uint8_t actCount,
                       uint8_t ackCount,
                       uint8_t clrCount)
{
    const int16_t bodyTop = kHeaderH + 4;
    char summary[48] = {0};
    snprintf(summary, sizeof(summary), "Alarmes: %u ACT   %u ACK   %u CLR",
             (unsigned)actCount,
             (unsigned)ackCount,
             (unsigned)clrCount);

    d.fillRect(0, bodyTop, w, (int16_t)(kAlarmSummaryH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSansBold9pt7b, kColorText, kColorBg, kSidePad, bodyTop, kAlarmSummaryH, summary);
}

void drawAlarmRow_(SupervisorSt7789& d,
                   bool swapBytes,
                   int16_t w,
                   uint8_t rowIndex,
                   const char* label,
                   SupervisorAlarmState state)
{
    const int16_t bodyTop = (int16_t)(kHeaderH + 4 + kAlarmSummaryH);
    const int16_t rowH = (int16_t)((d.height() - bodyTop) / (int16_t)kSupervisorAlarmSlotCount);
    const int16_t y = (int16_t)(bodyTop + ((int16_t)rowIndex * rowH));
    const int16_t pillX = (int16_t)(w - kSidePad - kAlarmPillW);
    const int16_t pillY = (int16_t)(y + ((rowH - kAlarmPillH) / 2));

    d.fillRect(0, y, w, (int16_t)(rowH - 1), panelColor_(swapBytes, kColorBg));
    drawGfxTextCenteredY_(d, swapBytes, &FreeSans9pt7b, kColorText, kColorBg, kSidePad, y, rowH, label);
    drawAlarmStatePill_(d, swapBytes, pillX, pillY, state);
}

void drawAlarmBody_(SupervisorSt7789& d, bool swapBytes, int16_t w, const SupervisorHmiViewModel& vm)
{
    drawAlarmSummary_(d, swapBytes, w, vm.flowAlarmActCount, vm.flowAlarmAckCount, vm.flowAlarmClrCount);
    for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        drawAlarmRow_(d, swapBytes, w, i, kAlarmRows[i].label, vm.flowAlarmStates[i]);
    }
}

float gaugeValueToAngle_(float value, float minValue, float maxValue)
{
    if (!(maxValue > minValue)) return -100.0f;
    const float ratio = (clampf_(value, minValue, maxValue) - minValue) / (maxValue - minValue);
    return -100.0f + (ratio * 200.0f);
}

void polarPoint_(int16_t cx, int16_t cy, float radius, float angleDeg, int16_t& x, int16_t& y)
{
    const float radians = angleDeg * 0.01745329252f;
    x = (int16_t)lroundf((float)cx + (cosf(radians) * radius));
    y = (int16_t)lroundf((float)cy + (sinf(radians) * radius));
}

void drawArcStroke_(SupervisorSt7789& d,
                    bool swapBytes,
                    int16_t cx,
                    int16_t cy,
                    int16_t radius,
                    float startDeg,
                    float endDeg,
                    uint16_t color,
                    int16_t thickness)
{
    if (!(endDeg > startDeg)) return;
    const int16_t half = (int16_t)(thickness / 2);
    for (int16_t offset = -half; offset <= half; ++offset) {
        const int16_t rr = (int16_t)(radius + offset);
        int16_t prevX = 0;
        int16_t prevY = 0;
        bool havePrev = false;
        for (float angle = startDeg; angle <= endDeg; angle += 4.0f) {
            int16_t x = 0;
            int16_t y = 0;
            polarPoint_(cx, cy, (float)rr, angle, x, y);
            if (havePrev) {
                d.drawLine(prevX, prevY, x, y, panelColor_(swapBytes, color));
            }
            prevX = x;
            prevY = y;
            havePrev = true;
        }
        int16_t endX = 0;
        int16_t endY = 0;
        polarPoint_(cx, cy, (float)rr, endDeg, endX, endY);
        d.drawLine(prevX, prevY, endX, endY, panelColor_(swapBytes, color));
    }
}

uint16_t resolveGaugeColor_(float value, bool hasValue, const GaugeBand* bands, size_t bandCount)
{
    if (!hasValue || !bands || bandCount == 0U) return kColorMuted;
    const float clamped = clampf_(value, bands[0].from, bands[bandCount - 1U].to);
    for (size_t i = 0; i < bandCount; ++i) {
        if (clamped >= bands[i].from && clamped <= bands[i].to) return bands[i].color;
    }
    return bands[bandCount - 1U].color;
}

void drawGaugeMarker_(SupervisorSt7789& d,
                      bool swapBytes,
                      int16_t cx,
                      int16_t cy,
                      int16_t radius,
                      float angleDeg,
                      uint16_t color)
{
    int16_t tipX = 0;
    int16_t tipY = 0;
    int16_t baseLX = 0;
    int16_t baseLY = 0;
    int16_t baseRX = 0;
    int16_t baseRY = 0;
    polarPoint_(cx, cy, (float)(radius + 4), angleDeg, tipX, tipY);
    polarPoint_(cx, cy, (float)(radius + 15), angleDeg - 5.5f, baseLX, baseLY);
    polarPoint_(cx, cy, (float)(radius + 15), angleDeg + 5.5f, baseRX, baseRY);
    d.fillTriangle(tipX, tipY, baseLX, baseLY, baseRX, baseRY, panelColor_(swapBytes, color));
}

void formatGaugeValue_(char* out, size_t outLen, bool hasValue, float value, uint8_t decimals, const char* unit)
{
    if (!out || outLen == 0U) return;
    if (!hasValue) {
        snprintf(out, outLen, "--");
        return;
    }
    char numberBuf[24] = {0};
    if (decimals > 0U) {
        snprintf(numberBuf, sizeof(numberBuf), "%.*f", (int)decimals, (double)value);
    } else {
        snprintf(numberBuf, sizeof(numberBuf), "%ld", lroundf(value));
    }
    if (unit && unit[0] != '\0') {
        snprintf(out, outLen, "%s %s", numberBuf, unit);
        return;
    }
    snprintf(out, outLen, "%s", numberBuf);
}

void drawMeasureGauge_(SupervisorSt7789& d,
                       bool swapBytes,
                       int16_t x,
                       int16_t y,
                       int16_t w,
                       int16_t h,
                       const char* label,
                       bool hasValue,
                       float value,
                       float minValue,
                       float maxValue,
                       uint8_t decimals,
                       const char* unit,
                       const GaugeBand* bands,
                       size_t bandCount)
{
    d.fillRoundRect(x, y, w, h, 10, panelColor_(swapBytes, kColorGaugeCardBg));
    d.drawRoundRect(x, y, w, h, 10, panelColor_(swapBytes, kColorCardBorder));

    uint16_t accent = kColorWater;
    if (label && strcmp(label, "Air") == 0) accent = kColorAir;
    else if (label && strcmp(label, "pH") == 0) accent = kColorPh;
    else if (label && strcmp(label, "ORP") == 0) accent = kColorOrp;
    drawCardLabel_(d, swapBytes, (int16_t)(x + 10), (int16_t)(y + 7), accent, label ? label : "");

    char valueBuf[24] = {0};
    formatGaugeValue_(valueBuf, sizeof(valueBuf), hasValue, value, decimals, nullptr);
    setGfxFont_(d, swapBytes, &FreeSansBold12pt7b, hasValue ? kColorValue : kColorMuted, kColorGaugeCardBg);
    const int16_t valueBaseY = (int16_t)(y + h - 14);
    d.setCursor((int16_t)(x + 12), valueBaseY);
    d.print(valueBuf);

    if (unit && unit[0] != '\0') {
        setGfxFont_(d, swapBytes, &FreeSans9pt7b, kColorMuted, kColorGaugeCardBg);
        if ((uint8_t)unit[0] == 0xB0 && unit[1] == 'C' && unit[2] == '\0') {
            const int16_t cw = gfxTextWidth_(d, "C");
            const int16_t unitX = (int16_t)(x + w - cw - 18);
            const int16_t unitY = (int16_t)(y + 15);
            d.drawCircle((int16_t)(unitX - 6), (int16_t)(unitY - 11), 2, panelColor_(swapBytes, kColorMuted));
            d.setCursor(unitX, unitY);
            d.print("C");
        } else {
            const int16_t unitW = gfxTextWidth_(d, unit);
            const int16_t unitX = (int16_t)(x + w - unitW - 12);
            d.setCursor(unitX, (int16_t)(y + 15));
            d.print(unit);
        }
    }

    (void)minValue;
    (void)maxValue;
    (void)bands;
    (void)bandCount;
}

void drawMeasuresBody_(SupervisorSt7789& d, bool swapBytes, int16_t w, int16_t h, const SupervisorHmiViewModel& vm)
{
    const int16_t heroY = (int16_t)(kHeaderH + 4);
    const int16_t heroH = 46;
    const int16_t heroX = kSidePad;
    const int16_t heroW = (int16_t)(w - (2 * kSidePad));
    const uint8_t systemState = systemState_(vm);
    const int16_t heroWordmarkX = (int16_t)(heroX + 10);
    const int16_t heroWordmarkW = 119;
    const int16_t heroWordmarkH = 27;
    const int16_t heroWordmarkY = (int16_t)(heroY + ((heroH - heroWordmarkH) / 2));
    const int16_t heroStatusX = (int16_t)(heroWordmarkX + heroWordmarkW + 10);
    const int16_t heroStatusW = (int16_t)((heroX + heroW) - heroStatusX - 10);

    d.fillRoundRect(heroX, heroY, heroW, heroH, 12, panelColor_(swapBytes, kColorGaugeCardBg));
    d.drawRoundRect(heroX, heroY, heroW, heroH, 12, panelColor_(swapBytes, kColorCardBorder));
    drawLogoWordmark_(d, swapBytes, heroWordmarkX, heroWordmarkY, heroWordmarkW, heroWordmarkH);
    drawSystemStatus_(d,
                      swapBytes,
                      heroStatusX,
                      (int16_t)(heroY + 11),
                      heroStatusW,
                      systemState);

    const int16_t bodyTop = (int16_t)(heroY + heroH + 6);
    const int16_t bodyBottomPad = 6;
    const int16_t gap = 4;
    const int16_t cardW = (int16_t)((w - (2 * kSidePad) - gap) / 2);
    const int16_t cardH = (int16_t)((h - bodyTop - bodyBottomPad - gap) / 2);
    const int16_t xLeft = kSidePad;
    const int16_t xRight = (int16_t)(kSidePad + cardW + gap);
    const int16_t yTop = bodyTop;
    const int16_t yBottom = (int16_t)(bodyTop + cardH + gap);

    drawMeasureGauge_(d,
                      swapBytes,
                      xLeft,
                      yTop,
                      cardW,
                      cardH,
                      "Eau",
                      vm.flowHasWaterTemp,
                      vm.flowWaterTemp,
                      0.0f,
                      40.0f,
                      1,
                      "\xB0""C",
                      kWaterTempGaugeBands,
                      sizeof(kWaterTempGaugeBands) / sizeof(kWaterTempGaugeBands[0]));
    drawMeasureGauge_(d,
                      swapBytes,
                      xRight,
                      yTop,
                      cardW,
                      cardH,
                      "Air",
                      vm.flowHasAirTemp,
                      vm.flowAirTemp,
                      -10.0f,
                      45.0f,
                      1,
                      "\xB0""C",
                      kAirTempGaugeBands,
                      sizeof(kAirTempGaugeBands) / sizeof(kAirTempGaugeBands[0]));
    drawMeasureGauge_(d,
                      swapBytes,
                      xLeft,
                      yBottom,
                      cardW,
                      cardH,
                      "pH",
                      vm.flowHasPh,
                      vm.flowPhValue,
                      6.4f,
                      8.4f,
                      1,
                      "",
                      kPhGaugeBands,
                      sizeof(kPhGaugeBands) / sizeof(kPhGaugeBands[0]));
    drawMeasureGauge_(d,
                      swapBytes,
                      xRight,
                      yBottom,
                      cardW,
                      cardH,
                      "ORP",
                      vm.flowHasOrp,
                      vm.flowOrpValue,
                      350.0f,
                      900.0f,
                      0,
                      "mV",
                      kOrpGaugeBands,
                      sizeof(kOrpGaugeBands) / sizeof(kOrpGaugeBands[0]));
}
}

St7789SupervisorDriver::St7789SupervisorDriver(const St7789SupervisorDriverConfig& cfg)
    : cfg_(cfg),
      display_(&spiBus_, cfg.csPin, cfg.dcPin, cfg.rstPin)
{
}

bool St7789SupervisorDriver::begin()
{
    if (started_) return true;
    const bool swapBytes = cfg_.swapColorBytes;
    spiBus_.begin(cfg_.sclkPin, -1, cfg_.mosiPin, cfg_.csPin);
    display_.setSPISpeed(cfg_.spiHz);
    display_.init(cfg_.resX, cfg_.resY);
    display_.setColRowStart(cfg_.colStart, cfg_.rowStart);
    display_.setRotation(cfg_.rotation & 0x03U);
    display_.invertDisplay(cfg_.invertColors);
    display_.fillScreen(panelColor_(swapBytes, kColorBg));
    display_.setTextWrap(false);
    display_.setTextSize(1);

    if (cfg_.backlightPin >= 0) {
        pinMode(cfg_.backlightPin, OUTPUT);
        digitalWrite(cfg_.backlightPin, HIGH);
        backlightOn_ = true;
    }

    drawBootLogo_(display_, swapBytes);
    delay(450);

    started_ = true;
    layoutDrawn_ = false;
    lastRenderMs_ = 0;
    lastTime_[0] = '\0';
    lastDate_[0] = '\0';
    lastIp_[0] = '\0';
    lastHasRssi_ = false;
    lastRssiDbm_ = -127;
    lastMqttReady_ = false;
    lastSystemState_ = 0xFFU;
    memset(lastRows_, 0, sizeof(lastRows_));
    lastHasPh_ = false;
    lastPhValue_ = 0.0f;
    lastHasOrp_ = false;
    lastOrpValue_ = 0.0f;
    lastHasWaterTemp_ = false;
    lastWaterTemp_ = 0.0f;
    lastHasAirTemp_ = false;
    lastAirTemp_ = 0.0f;
    lastPage_ = 0xFFU;
    lastAlarmActCount_ = 0xFFU;
    lastAlarmAckCount_ = 0xFFU;
    lastAlarmClrCount_ = 0xFFU;
    for (uint8_t i = 0; i < kSupervisorAlarmSlotCount; ++i) {
        lastAlarmStates_[i] = SupervisorAlarmState::Clear;
    }
    return true;
}

void St7789SupervisorDriver::setBacklight(bool on)
{
    if (cfg_.backlightPin < 0) return;
    if (backlightOn_ == on) return;
    digitalWrite(cfg_.backlightPin, on ? HIGH : LOW);
    backlightOn_ = on;
}

const char* St7789SupervisorDriver::wifiStateText_(WifiState st) const
{
    switch (st) {
        case WifiState::Disabled: return "disabled";
        case WifiState::Idle: return "idle";
        case WifiState::Connecting: return "connecting";
        case WifiState::Connected: return "connected";
        case WifiState::ErrorWait: return "retry_wait";
        default: return "?";
    }
}

const char* St7789SupervisorDriver::netModeText_(NetworkAccessMode mode) const
{
    switch (mode) {
        case NetworkAccessMode::Station: return "sta";
        case NetworkAccessMode::AccessPoint: return "ap";
        case NetworkAccessMode::None:
        default:
            return "none";
    }
}

bool St7789SupervisorDriver::render(const SupervisorHmiViewModel& vm, bool force)
{
    if (!started_) return false;
    const uint32_t now = millis();
    if (!force && cfg_.minRenderGapMs > 0U && (uint32_t)(now - lastRenderMs_) < cfg_.minRenderGapMs) {
        return true;
    }

    const int16_t w = display_.width();
    const int16_t h = display_.height();
    const bool swapBytes = cfg_.swapColorBytes;
    const int32_t activeRssi = vm.flowHasRssi ? vm.flowRssiDbm : vm.rssiDbm;
    const bool hasAnyRssi = vm.flowHasRssi || vm.hasRssi;
    const uint8_t systemState = systemState_(vm);
    const SupervisorPage page = SupervisorPage::Measures;

    char timeBuf[16] = "--:--";
    time_t t = time(nullptr);
    if (t > 1600000000) {
        struct tm tmv{};
        localtime_r(&t, &tmv);
        (void)strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tmv);
    }

    const bool pageChanged = (lastPage_ != (uint8_t)page);
    if (!layoutDrawn_ || force || pageChanged) {
        drawStaticLayout_(display_, swapBytes, w, h, page);
        layoutDrawn_ = true;
        lastPage_ = (uint8_t)page;
        lastTime_[0] = '\0';
        lastHasRssi_ = !hasAnyRssi;
        lastRssiDbm_ = hasAnyRssi ? (activeRssi - 1) : -126;
        lastMqttReady_ = !vm.flowMqttReady;
        lastSystemState_ = 0xFFU;
        lastHasPh_ = !vm.flowHasPh;
        lastPhValue_ = vm.flowPhValue - 1.0f;
        lastHasOrp_ = !vm.flowHasOrp;
        lastOrpValue_ = vm.flowOrpValue - 1.0f;
        lastHasWaterTemp_ = !vm.flowHasWaterTemp;
        lastWaterTemp_ = vm.flowWaterTemp - 1.0f;
        lastHasAirTemp_ = !vm.flowHasAirTemp;
        lastAirTemp_ = vm.flowAirTemp - 1.0f;
        lastAlarmActCount_ = 0xFFU;
        lastAlarmAckCount_ = 0xFFU;
        lastAlarmClrCount_ = 0xFFU;
    }

    if (lastHasRssi_ != hasAnyRssi || lastRssiDbm_ != activeRssi) {
        drawHeaderWifi_(display_, swapBytes, hasAnyRssi, activeRssi);
        lastHasRssi_ = hasAnyRssi;
        lastRssiDbm_ = activeRssi;
    }

    if (strcmp(lastTime_, timeBuf) != 0) {
        drawHeaderTime_(display_, swapBytes, w, timeBuf);
        snprintf(lastTime_, sizeof(lastTime_), "%s", timeBuf);
    }

    if (strcmp(lastTime_, timeBuf) == 0 && lastMqttReady_ != vm.flowMqttReady) {
        drawHeaderMqtt_(display_, swapBytes, w, timeBuf, vm.flowMqttReady);
        lastMqttReady_ = vm.flowMqttReady;
    } else if (lastMqttReady_ != vm.flowMqttReady) {
        drawHeaderMqtt_(display_, swapBytes, w, timeBuf, vm.flowMqttReady);
        lastMqttReady_ = vm.flowMqttReady;
    }

    const bool measuresChanged = force || pageChanged ||
                                 (lastSystemState_ != systemState) ||
                                 (lastHasPh_ != vm.flowHasPh) ||
                                 (fabsf(lastPhValue_ - vm.flowPhValue) > 0.005f) ||
                                 (lastHasOrp_ != vm.flowHasOrp) ||
                                 (fabsf(lastOrpValue_ - vm.flowOrpValue) > 0.5f) ||
                                 (lastHasWaterTemp_ != vm.flowHasWaterTemp) ||
                                 (fabsf(lastWaterTemp_ - vm.flowWaterTemp) > 0.05f) ||
                                 (lastHasAirTemp_ != vm.flowHasAirTemp) ||
                                 (fabsf(lastAirTemp_ - vm.flowAirTemp) > 0.05f);
    if (measuresChanged) {
        drawMeasuresBody_(display_, swapBytes, w, h, vm);
        lastSystemState_ = systemState;
        lastHasPh_ = vm.flowHasPh;
        lastPhValue_ = vm.flowPhValue;
        lastHasOrp_ = vm.flowHasOrp;
        lastOrpValue_ = vm.flowOrpValue;
        lastHasWaterTemp_ = vm.flowHasWaterTemp;
        lastWaterTemp_ = vm.flowWaterTemp;
        lastHasAirTemp_ = vm.flowHasAirTemp;
        lastAirTemp_ = vm.flowAirTemp;
    }

    setDefaultFont_(display_, swapBytes, kColorText, kColorBg, 1);

    layoutDrawn_ = true;
    lastRenderMs_ = now;
    return true;
}
