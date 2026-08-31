// Host shim: just enough ChibiOS RTC to compile the OLED/pomodoro code.
#pragma once
#include <stdint.h>

typedef struct {
    uint32_t year : 8;        // years since 1980
    uint32_t month : 4;       // 1..12
    uint32_t dstflag : 1;
    uint32_t dayofweek : 3;
    uint32_t day : 5;         // 1..31
    uint32_t millisecond : 27; // ms since midnight
} RTCDateTime;

typedef int RTCDriver;
extern RTCDriver RTCD1;

void rtcGetTime(RTCDriver *rtcp, RTCDateTime *t);
void rtcSetTime(RTCDriver *rtcp, const RTCDateTime *t);
