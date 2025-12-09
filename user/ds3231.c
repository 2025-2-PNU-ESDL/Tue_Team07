#include "ds3231.h"

// CubeMX에서 설정한 I2C 핸들러를 가져옴
extern I2C_HandleTypeDef hi2c1; 

// 레지스터 주소 정의
#define DS3231_CONTROL_REG 0x0E
#define DS3231_STATUS_REG  0x0F
#define DS3231_ALARM1_ADDR 0x07
#define DS3231_ALARM2_ADDR 0x0B

// BCD 변환 함수 (내부용)
uint8_t decToBcd(int val) { return (uint8_t)( (val/10*16) + (val%10) ); }
int bcdToDec(uint8_t val) { return (int)( (val/16*10) + (val%16) ); }

void DS3231_Init(I2C_HandleTypeDef *hi2c) {
    // 필요 시 초기 설정 (대부분 불필요)
}

void DS3231_GetTime(RTC_TimeTypeDef *rtc_time) {
    uint8_t buffer[7];
    HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDRESS, 0x00, 1, buffer, 7, 1000);

    rtc_time->seconds = bcdToDec(buffer[0]);
    rtc_time->minutes = bcdToDec(buffer[1]);
    rtc_time->hours   = bcdToDec(buffer[2]);
    rtc_time->dayofweek = bcdToDec(buffer[3]);
    rtc_time->dayofmonth = bcdToDec(buffer[4]);
    rtc_time->month   = bcdToDec(buffer[5]);
    rtc_time->year    = bcdToDec(buffer[6]);
}

void DS3231_SetTime(RTC_TimeTypeDef *rtc_time) {
    uint8_t buffer[7];
    buffer[0] = decToBcd(rtc_time->seconds);
    buffer[1] = decToBcd(rtc_time->minutes);
    buffer[2] = decToBcd(rtc_time->hours);
    buffer[3] = decToBcd(rtc_time->dayofweek);
    buffer[4] = decToBcd(rtc_time->dayofmonth);
    buffer[5] = decToBcd(rtc_time->month);
    buffer[6] = decToBcd(rtc_time->year);

    HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS, 0x00, 1, buffer, 7, 1000);
}

void DS3231_ClearAlarmFlags(void) {
    uint8_t status;
    // 상태 레지스터 읽기 및 A1F, A2F 플래그 클리어 (0으로 씀)
    HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDRESS, DS3231_STATUS_REG, 1, &status, 1, 100);
    status &= ~0x03; // Clear A1F and A2F
    HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS, DS3231_STATUS_REG, 1, &status, 1, 100);
}

// 매일 특정 시간에 울리는 알람 1 설정 (초 단위 포함)
void DS3231_SetAlarm1(uint8_t hour, uint8_t min, uint8_t sec) {
    uint8_t buff[4];
    buff[0] = decToBcd(sec);       // A1M1 = 0 (Match seconds)
    buff[1] = decToBcd(min);       // A1M2 = 0 (Match minutes)
    buff[2] = decToBcd(hour);      // A1M3 = 0 (Match hours)
    buff[3] = 0x80;                // A1M4 = 1 (Day/Date 무시 -> 매일 반복)
    
    HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS, DS3231_ALARM1_ADDR, 1, buff, 4, 100);
    
    // 인터럽트 활성화 (INTCN=1, A1IE=1)
    uint8_t ctrl;
    HAL_I2C_Mem_Read(&hi2c1, DS3231_ADDRESS, DS3231_CONTROL_REG, 1, &ctrl, 1, 100);
    ctrl |= 0x05; // INTCN(bit2) | A1IE(bit0)
    HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS, DS3231_CONTROL_REG, 1, &ctrl, 1, 100);
}

void DS3231_SetAlarm2(uint8_t hour, uint8_t min) {
    uint8_t buff[3];
    // A2M2 = 0 (분 매치), A2M3 = 0 (시 매치), A2M4 = 1 (요일/날짜 무시 -> 매일)
    buff[0] = decToBcd(min);   // 분
    buff[1] = decToBcd(hour);  // 시
    buff[2] = 0x80;            // 매일 반복

    HAL_I2C_Mem_Write(&hi2c1, DS3231_ADDRESS,
                      DS3231_ALARM2_ADDR, 1, buff, 3, 100);

    // INTCN(bit2) + A2IE(bit1) 활성화
    uint8_t ctrl;
    HAL_I2C_Mem_Read(&hi2c1, DS3231_CONTROL_REG, 1, &ctrl, 1, 100);
    ctrl |= 0x06;   // 0b0000 0110
    HAL_I2C_Mem_Write(&hi2c1, DS3231_CONTROL_REG, 1, &ctrl, 1, 100);
}
