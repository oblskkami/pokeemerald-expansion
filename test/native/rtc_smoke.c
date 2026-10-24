#include <stdio.h>

#include "gba/types.h"
#include "constants/siirtc.h"
#include "siirtc.h"

static int BcdToInt(u8 value)
{
    if ((value & 0xF) > 9 || ((value >> 4) & 0xF) > 9)
        return -1;
    return ((value >> 4) * 10) + (value & 0xF);
}

static int CheckDateTime(const struct SiiRtcInfo *rtc, int year, int month, int day,
                         int hour, int minute, int second)
{
    int actualSecond = BcdToInt(rtc->second);

    return BcdToInt(rtc->year) == year
        && BcdToInt(rtc->month) == month
        && BcdToInt(rtc->day) == day
        && BcdToInt(rtc->hour) == hour
        && BcdToInt(rtc->minute) == minute
        && (actualSecond == second || actualSecond == second + 1);
}

int main(void)
{
    struct SiiRtcInfo status = {0};
    struct SiiRtcInfo rtc = {0};
    struct SiiRtcInfo desired = {
        .year = 0x27,
        .month = 0x07,
        .day = 0x08,
        .hour = 0x09,
        .minute = 0x10,
        .second = 0x11,
    };
    struct SiiRtcInfo invalid = desired;
    struct SiiRtcInfo desiredTime = {
        .hour = 0x13,
        .minute = 0x14,
        .second = 0x15,
    };

    if (!SiiRtcReset()
     || SiiRtcProbe() != 1
     || !SiiRtcGetStatus(&status)
     || status.status != SIIRTCINFO_24HOUR
     || !SiiRtcGetDateTime(&rtc)
     || BcdToInt(rtc.month) < 1 || BcdToInt(rtc.month) > 12
     || BcdToInt(rtc.day) < 1 || BcdToInt(rtc.day) > 31
     || BcdToInt(rtc.hour) > 23 || BcdToInt(rtc.minute) > 59
     || BcdToInt(rtc.second) > 59)
    {
        fprintf(stderr, "portable RTC reset/read check failed\n");
        return 1;
    }

    status.status = SIIRTCINFO_POWER | SIIRTCINFO_INTFE | SIIRTCINFO_INTAE;
    if (!SiiRtcSetStatus(&status)
     || !SiiRtcGetStatus(&status)
     || status.status != (SIIRTCINFO_24HOUR | SIIRTCINFO_INTFE | SIIRTCINFO_INTAE))
    {
        fprintf(stderr, "portable RTC status check failed\n");
        return 1;
    }

    invalid.month = 0x02;
    invalid.day = 0x30;
    if (SiiRtcSetDateTime(&invalid)
     || !SiiRtcSetDateTime(&desired)
     || !SiiRtcGetDateTime(&rtc)
     || !CheckDateTime(&rtc, 27, 7, 8, 9, 10, 11)
     || !SiiRtcGetStatus(&status)
     || status.status != (SIIRTCINFO_24HOUR | SIIRTCINFO_INTFE | SIIRTCINFO_INTAE))
    {
        fprintf(stderr, "portable RTC date check failed\n");
        return 1;
    }

    if (!SiiRtcSetTime(&desiredTime)
     || !SiiRtcGetDateTime(&rtc)
     || !CheckDateTime(&rtc, 27, 7, 8, 13, 14, 15)
     || !SiiRtcReset()
     || SiiRtcProbe() != 1)
    {
        fprintf(stderr, "portable RTC time check failed\n");
        return 1;
    }

    puts("native RTC smoke checks passed");
    return 0;
}
