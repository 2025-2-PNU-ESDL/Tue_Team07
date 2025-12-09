#ifndef DS3231_H
#define DS3231_H

#include "main.h"

// 주소 (0x68 << 1)
#define DS3231_ADDRESS 0xD0

// 시간 구조체 정의
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t dayofweek;
    uint8_t dayofmonth;
    uint8_t month;
    uint8_t year;
} RTC_TimeTypeDef;

// 함수 원형
// void DS3231_Init(I2C_HandleTypeDef *hi2c);
void DS3231_Init(void);
void DS3231_GetTime(RTC_TimeTypeDef *rtc_time);
void DS3231_SetTime(RTC_TimeTypeDef *rtc_time);

// 알람 설정용 구조체 및 함수 추가
void DS3231_SetAlarm1(uint8_t hour, uint8_t min, uint8_t sec); // 시작 시간 (매일)
void DS3231_SetAlarm2(uint8_t hour, uint8_t min);             // 마감 시간 (매일)
void DS3231_ClearAlarmFlags(void);                            // 인터럽트 플래그 초기화

#endif