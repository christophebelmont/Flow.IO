/**
 * @file NextionDriver.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/Drivers/NextionDriver.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static constexpr uint8_t NEXTION_FF = 0xFF;
static constexpr uint8_t NEXTION_EVT_TOUCH = 0x65;
static constexpr uint8_t NEXTION_EVT_STR = 0x70;

static constexpr uint8_t CMP_HOME = 10;
static constexpr uint8_t CMP_BACK = 11;
static constexpr uint8_t CMP_VALIDATE = 12;
static constexpr uint8_t CMP_PREV_PAGE = 13;
static constexpr uint8_t CMP_NEXT_PAGE = 14;
static constexpr uint8_t CMP_ROW_BASE = 20;
static constexpr uint8_t CMP_ROW_LAST = CMP_ROW_BASE + 5;

static bool startsWith_(const char* s, const char* prefix)
{
    if (!s || !prefix) return false;
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

} // namespace

bool NextionDriver::begin()
{
    if (started_) return true;
    if (!cfg_.serial) return false;

    if (cfg_.rxPin >= 0 && cfg_.txPin >= 0) {
        cfg_.serial->begin(cfg_.baud, SERIAL_8N1, cfg_.rxPin, cfg_.txPin);
    } else {
        cfg_.serial->begin(cfg_.baud);
    }
    delay(30);
    started_ = true;
    pageReady_ = false;
    lastRenderMs_ = 0;
    rxLen_ = 0;
    ffCount_ = 0;
    return true;
}

void NextionDriver::tick(uint32_t)
{
}

bool NextionDriver::sendCmd_(const char* cmd)
{
    if (!started_ || !cfg_.serial || !cmd) return false;
    cfg_.serial->print(cmd);
    cfg_.serial->write(NEXTION_FF);
    cfg_.serial->write(NEXTION_FF);
    cfg_.serial->write(NEXTION_FF);
    return true;
}

bool NextionDriver::sendCmdFmt_(const char* fmt, ...)
{
    if (!fmt) return false;
    char cmd[160]{};
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return false;
    return sendCmd_(cmd);
}

void NextionDriver::sanitizeText_(char* out, size_t outLen, const char* in) const
{
    if (!out || outLen == 0) return;
    if (!in) in = "";

    size_t pos = 0;
    for (size_t i = 0; in[i] != '\0' && pos + 1 < outLen; ++i) {
        const char c = in[i];
        if ((unsigned char)c < 32 || (unsigned char)c > 126) continue;
        if (c == '"' || c == '\\') {
            if (pos + 2 >= outLen) break;
            out[pos++] = '\\';
            out[pos++] = c;
            continue;
        }
        out[pos++] = c;
    }
    out[pos] = '\0';
}

bool NextionDriver::sendText_(const char* objectName, const char* value)
{
    if (!objectName || objectName[0] == '\0') return false;
    char safe[96]{};
    sanitizeText_(safe, sizeof(safe), value);
    return sendCmdFmt_("%s.txt=\"%s\"", objectName, safe);
}

bool NextionDriver::renderConfigMenu(const ConfigMenuView& view)
{
    if (!started_) return false;
    const uint32_t now = millis();
    if (cfg_.minRenderGapMs > 0 && (uint32_t)(now - lastRenderMs_) < cfg_.minRenderGapMs) {
        return true;
    }

    if (!pageReady_) {
        (void)sendCmd_("page pageCfgMenu");
        pageReady_ = true;
    }

    (void)sendText_("tPath", view.breadcrumb);
    (void)sendCmdFmt_("vis bHome,%u", view.canHome ? 1U : 0U);
    (void)sendCmdFmt_("vis bBack,%u", view.canBack ? 1U : 0U);
    (void)sendCmdFmt_("vis bValid,%u", view.canValidate ? 1U : 0U);
    (void)sendCmdFmt_("vis bPrev,%u", (view.pageIndex > 0U) ? 1U : 0U);
    (void)sendCmdFmt_("vis bNext,%u", (view.pageIndex + 1U < view.pageCount) ? 1U : 0U);
    (void)sendCmdFmt_("nPage.val=%u", (unsigned)(view.pageIndex + 1U));
    (void)sendCmdFmt_("nPages.val=%u", (unsigned)view.pageCount);

    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        char leftObj[8]{};
        char rightObj[8]{};
        char touchObj[8]{};
        snprintf(leftObj, sizeof(leftObj), "tL%u", (unsigned)i);
        snprintf(rightObj, sizeof(rightObj), "tV%u", (unsigned)i);
        snprintf(touchObj, sizeof(touchObj), "bR%u", (unsigned)i);

        const ConfigMenuRowView& row = view.rows[i];
        (void)sendCmdFmt_("vis %s,%u", leftObj, row.visible ? 1U : 0U);
        (void)sendCmdFmt_("vis %s,%u", rightObj, row.visible ? 1U : 0U);
        (void)sendCmdFmt_("vis %s,%u", touchObj, row.visible ? 1U : 0U);
        if (!row.visible) continue;

        (void)sendText_(leftObj, row.label);

        char displayVal[64]{};
        snprintf(displayVal, sizeof(displayVal), "%s%s", row.value, row.dirty ? " *" : "");
        (void)sendText_(rightObj, displayVal);

        uint32_t color = 65535U;
        if (row.widget == ConfigMenuWidget::Switch) color = 2047U;
        else if (row.widget == ConfigMenuWidget::Slider) color = 65504U;
        else if (row.widget == ConfigMenuWidget::Select) color = 2016U;
        if (!row.editable) color = 33840U;
        if (row.dirty) color = 64800U;
        (void)sendCmdFmt_("%s.pco=%lu", rightObj, (unsigned long)color);
    }

    lastRenderMs_ = now;
    return true;
}

bool NextionDriver::parseTouchEvent_(const uint8_t* frame, uint8_t len, HmiEvent& out)
{
    if (!frame || len < 4 || frame[0] != NEXTION_EVT_TOUCH) return false;
    const uint8_t cmp = frame[2];
    const uint8_t state = frame[3];
    if (state == 0) return false; // handle press only

    if (cmp == CMP_HOME) { out.type = HmiEventType::Home; return true; }
    if (cmp == CMP_BACK) { out.type = HmiEventType::Back; return true; }
    if (cmp == CMP_VALIDATE) { out.type = HmiEventType::Validate; return true; }
    if (cmp == CMP_NEXT_PAGE) { out.type = HmiEventType::NextPage; return true; }
    if (cmp == CMP_PREV_PAGE) { out.type = HmiEventType::PrevPage; return true; }
    if (cmp >= CMP_ROW_BASE && cmp <= CMP_ROW_LAST) {
        out.type = HmiEventType::RowActivate;
        out.row = (uint8_t)(cmp - CMP_ROW_BASE);
        return true;
    }
    return false;
}

bool NextionDriver::parseAsciiEvent_(const char* text, HmiEvent& out)
{
    if (!text || text[0] == '\0') return false;
    if (!startsWith_(text, "EV:")) return false;

    if (strcmp(text, "EV:HOME") == 0) { out.type = HmiEventType::Home; return true; }
    if (strcmp(text, "EV:BACK") == 0) { out.type = HmiEventType::Back; return true; }
    if (strcmp(text, "EV:VAL") == 0) { out.type = HmiEventType::Validate; return true; }
    if (strcmp(text, "EV:NEXT") == 0) { out.type = HmiEventType::NextPage; return true; }
    if (strcmp(text, "EV:PREV") == 0) { out.type = HmiEventType::PrevPage; return true; }

    if (startsWith_(text, "EV:ROW:")) {
        const int row = atoi(text + 7);
        if (row < 0 || row >= (int)ConfigMenuModel::RowsPerPage) return false;
        out.type = HmiEventType::RowActivate;
        out.row = (uint8_t)row;
        return true;
    }

    if (startsWith_(text, "EV:TOG:")) {
        const int row = atoi(text + 7);
        if (row < 0 || row >= (int)ConfigMenuModel::RowsPerPage) return false;
        out.type = HmiEventType::RowToggle;
        out.row = (uint8_t)row;
        return true;
    }

    if (startsWith_(text, "EV:CYC:")) {
        // EV:CYC:<row>:<dir>
        const char* p = text + 7;
        const int row = atoi(p);
        const char* sep = strchr(p, ':');
        if (!sep) return false;
        const int dir = atoi(sep + 1);
        if (row < 0 || row >= (int)ConfigMenuModel::RowsPerPage) return false;
        out.type = HmiEventType::RowCycle;
        out.row = (uint8_t)row;
        out.direction = (dir < 0) ? -1 : 1;
        return true;
    }

    if (startsWith_(text, "EV:SLD:")) {
        // EV:SLD:<row>:<value>
        const char* p = text + 7;
        const int row = atoi(p);
        const char* sep = strchr(p, ':');
        if (!sep) return false;
        if (row < 0 || row >= (int)ConfigMenuModel::RowsPerPage) return false;
        out.type = HmiEventType::RowSetSlider;
        out.row = (uint8_t)row;
        out.sliderValue = strtof(sep + 1, nullptr);
        return true;
    }

    if (startsWith_(text, "EV:TXT:")) {
        // EV:TXT:<row>:<value>
        const char* p = text + 7;
        const int row = atoi(p);
        const char* sep = strchr(p, ':');
        if (!sep) return false;
        if (row < 0 || row >= (int)ConfigMenuModel::RowsPerPage) return false;
        out.type = HmiEventType::RowSetText;
        out.row = (uint8_t)row;
        snprintf(out.text, sizeof(out.text), "%s", sep + 1);
        return true;
    }

    return false;
}

bool NextionDriver::parseFrame_(const uint8_t* frame, uint8_t len, HmiEvent& out)
{
    if (!frame || len == 0) return false;

    if (parseTouchEvent_(frame, len, out)) return true;

    if (frame[0] == NEXTION_EVT_STR && len > 1) {
        char tmp[96]{};
        const uint8_t copyLen = (len - 1U < sizeof(tmp) - 1U) ? (len - 1U) : (sizeof(tmp) - 1U);
        memcpy(tmp, frame + 1, copyLen);
        tmp[copyLen] = '\0';
        return parseAsciiEvent_(tmp, out);
    }

    if (frame[0] >= 32 && frame[0] <= 126) {
        char tmp[96]{};
        const uint8_t copyLen = (len < sizeof(tmp) - 1U) ? len : (sizeof(tmp) - 1U);
        memcpy(tmp, frame, copyLen);
        tmp[copyLen] = '\0';
        return parseAsciiEvent_(tmp, out);
    }

    return false;
}

bool NextionDriver::pollEvent(HmiEvent& out)
{
    out = HmiEvent{};
    if (!started_ || !cfg_.serial) return false;

    while (cfg_.serial->available() > 0) {
        const int rb = cfg_.serial->read();
        if (rb < 0) break;
        const uint8_t b = (uint8_t)rb;

        if (b == NEXTION_FF) {
            ++ffCount_;
            if (ffCount_ >= 3) {
                const bool parsed = parseFrame_(rxBuf_, rxLen_, out);
                rxLen_ = 0;
                ffCount_ = 0;
                if (parsed) return true;
            }
            continue;
        }

        while (ffCount_ > 0) {
            if (rxLen_ < RxBufSize) rxBuf_[rxLen_++] = NEXTION_FF;
            --ffCount_;
        }

        if (rxLen_ < RxBufSize) {
            rxBuf_[rxLen_++] = b;
        } else {
            rxLen_ = 0;
            ffCount_ = 0;
        }
    }

    return false;
}
