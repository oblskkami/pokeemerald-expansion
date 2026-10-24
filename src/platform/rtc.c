#ifdef PORTABLE

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "gba/types.h"
#include "constants/siirtc.h"
#include "siirtc.h"
#include "platform.h"

static time_t sRtcOffset;
static u8 sRtcStatus = SIIRTCINFO_24HOUR;

static u8 ToBcd(int value)
{
    return (u8)(((value / 10) << 4) | (value % 10));
}

static bool FromBcd(u8 value, int *result)
{
    int high = (value >> 4) & 0xF;
    int low = value & 0xF;

    if (high > 9 || low > 9)
        return false;
    *result = high * 10 + low;
    return true;
}

static time_t CurrentRtcTime(void)
{
    time_t now = time(NULL);

    if (now == (time_t)-1)
        return (time_t)-1;
    return now + sRtcOffset;
}

static bool GetLocalTime(time_t timestamp, struct tm *result)
{
    struct tm *value = localtime(&timestamp);

    if (value == NULL)
        return false;
    *result = *value;
    return true;
}

static bool DecodeDateTime(const struct SiiRtcInfo *rtc, struct tm *result)
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;

    if (rtc == NULL || result == NULL
     || !FromBcd(rtc->year, &year)
     || !FromBcd(rtc->month, &month)
     || !FromBcd(rtc->day, &day)
     || !FromBcd(rtc->hour & 0x7F, &hour)
     || !FromBcd(rtc->minute, &minute)
     || !FromBcd(rtc->second & 0x7F, &second)
     || month < 1 || month > 12 || day < 1 || day > 31
     || hour > 23 || minute > 59 || second > 59)
    {
        return false;
    }

    memset(result, 0, sizeof(*result));
    result->tm_year = 100 + year;
    result->tm_mon = month - 1;
    result->tm_mday = day;
    result->tm_hour = hour;
    result->tm_min = minute;
    result->tm_sec = second;
    result->tm_isdst = -1;
    return true;
}

bool Platform_RtcReset(void)
{
    sRtcOffset = 0;
    sRtcStatus = SIIRTCINFO_24HOUR;
    return true;
}

bool Platform_RtcGetStatus(struct SiiRtcInfo *rtc)
{
    if (rtc == NULL)
        return false;
    rtc->status = sRtcStatus;
    return true;
}

bool Platform_RtcSetStatus(const struct SiiRtcInfo *rtc)
{
    if (rtc == NULL)
        return false;
    sRtcStatus = (rtc->status & (SIIRTCINFO_INTFE | SIIRTCINFO_INTME | SIIRTCINFO_INTAE))
        | SIIRTCINFO_24HOUR;
    return true;
}

bool Platform_RtcGetDateTime(struct SiiRtcInfo *rtc)
{
    struct tm current;
    time_t timestamp = CurrentRtcTime();

    if (rtc == NULL || timestamp == (time_t)-1 || !GetLocalTime(timestamp, &current))
        return false;
    rtc->year = ToBcd(current.tm_year % 100);
    rtc->month = ToBcd(current.tm_mon + 1);
    rtc->day = ToBcd(current.tm_mday);
    rtc->dayOfWeek = (u8)current.tm_wday;
    rtc->hour = ToBcd(current.tm_hour);
    rtc->minute = ToBcd(current.tm_min);
    rtc->second = ToBcd(current.tm_sec);
    return true;
}

bool Platform_RtcSetDateTime(const struct SiiRtcInfo *rtc)
{
    struct tm desired;
    struct tm requested;
    time_t now;
    time_t target;

    if (rtc == NULL || !DecodeDateTime(rtc, &desired))
        return false;
    requested = desired;
    target = mktime(&desired);
    now = time(NULL);
    if (target == (time_t)-1 || now == (time_t)-1
     || desired.tm_year != requested.tm_year
     || desired.tm_mon != requested.tm_mon
     || desired.tm_mday != requested.tm_mday
     || desired.tm_hour != requested.tm_hour
     || desired.tm_min != requested.tm_min
     || desired.tm_sec != requested.tm_sec)
    {
        return false;
    }
    sRtcOffset = target - now;
    return true;
}

bool Platform_RtcGetTime(struct SiiRtcInfo *rtc)
{
    struct tm current;
    time_t timestamp = CurrentRtcTime();

    if (rtc == NULL || timestamp == (time_t)-1 || !GetLocalTime(timestamp, &current))
        return false;
    rtc->hour = ToBcd(current.tm_hour);
    rtc->minute = ToBcd(current.tm_min);
    rtc->second = ToBcd(current.tm_sec);
    return true;
}

bool Platform_RtcSetTime(const struct SiiRtcInfo *rtc)
{
    struct SiiRtcInfo current = {0};

    if (rtc == NULL || !Platform_RtcGetDateTime(&current))
        return false;
    current.hour = rtc->hour;
    current.minute = rtc->minute;
    current.second = rtc->second;
    return Platform_RtcSetDateTime(&current);
}

#endif // PORTABLE
