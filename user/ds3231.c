#include "ds3231.h"
#include "stm32f10x.h"

// Define I2C Interface
#define DS3231_I2C I2C1

// Register Definitions
#define DS3231_CONTROL_REG 0x0E
#define DS3231_STATUS_REG  0x0F
#define DS3231_ALARM1_ADDR 0x07
#define DS3231_ALARM2_ADDR 0x0B

// Helper Functions
uint8_t decToBcd(int val) { return (uint8_t)( (val/10*16) + (val%10) ); }
int bcdToDec(uint8_t val) { return (int)( (val/16*10) + (val%16) ); }

#define DS3231_TIMEOUT 20000
static uint8_t DS3231_I2C_Error = 0;

// Low-level I2C Write
void DS3231_WriteReg(uint8_t reg, uint8_t val) {
    if (DS3231_I2C_Error) return;

    uint32_t timeout = DS3231_TIMEOUT;
    while(I2C_GetFlagStatus(DS3231_I2C, I2C_FLAG_BUSY)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_GenerateSTART(DS3231_I2C, ENABLE);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_Send7bitAddress(DS3231_I2C, DS3231_ADDRESS, I2C_Direction_Transmitter);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    // Send Register Address
    I2C_SendData(DS3231_I2C, reg);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    // Send Value
    I2C_SendData(DS3231_I2C, val);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    // Generate Stop
    I2C_GenerateSTOP(DS3231_I2C, ENABLE);
}

// Low-level I2C Read
uint8_t DS3231_ReadReg(uint8_t reg) {
    //if (DS3231_I2C_Error) return 0;
    
    uint8_t val = 0;
    uint32_t timeout = DS3231_TIMEOUT;
    
    while(I2C_GetFlagStatus(DS3231_I2C, I2C_FLAG_BUSY)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return 0; }
    }

    I2C_GenerateSTART(DS3231_I2C, ENABLE);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return 0; }
    }

    I2C_Send7bitAddress(DS3231_I2C, DS3231_ADDRESS, I2C_Direction_Transmitter);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return 0; }
    }

    I2C_SendData(DS3231_I2C, reg);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return 0; }
    }

    I2C_GenerateSTART(DS3231_I2C, ENABLE);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return 0; }
    }

    I2C_Send7bitAddress(DS3231_I2C, DS3231_ADDRESS, I2C_Direction_Receiver);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return 0; }
    }

    I2C_AcknowledgeConfig(DS3231_I2C, DISABLE);
    I2C_GenerateSTOP(DS3231_I2C, ENABLE);

    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return 0; }
    }
    val = I2C_ReceiveData(DS3231_I2C);
    
    I2C_AcknowledgeConfig(DS3231_I2C, ENABLE);
    
    return val;
}

// Burst Read
void DS3231_ReadBurst(uint8_t reg, uint8_t *buf, uint16_t count) {
    if (DS3231_I2C_Error) return;

    uint32_t timeout = DS3231_TIMEOUT;
    while(I2C_GetFlagStatus(DS3231_I2C, I2C_FLAG_BUSY)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_GenerateSTART(DS3231_I2C, ENABLE);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_Send7bitAddress(DS3231_I2C, DS3231_ADDRESS, I2C_Direction_Transmitter);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_SendData(DS3231_I2C, reg);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_GenerateSTART(DS3231_I2C, ENABLE);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_Send7bitAddress(DS3231_I2C, DS3231_ADDRESS, I2C_Direction_Receiver);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    for(int i = 0; i < count; i++) {
        if(i == count - 1) {
            I2C_AcknowledgeConfig(DS3231_I2C, DISABLE);
            I2C_GenerateSTOP(DS3231_I2C, ENABLE);
        }
        
        timeout = DS3231_TIMEOUT;
        while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
            if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
        }
        buf[i] = I2C_ReceiveData(DS3231_I2C);
    }
    I2C_AcknowledgeConfig(DS3231_I2C, ENABLE);
}

// Burst Write (Used for Time Set)
void DS3231_WriteBurst(uint8_t reg, uint8_t *buf, uint16_t count) {
    if (DS3231_I2C_Error) return;

    uint32_t timeout = DS3231_TIMEOUT;
    while(I2C_GetFlagStatus(DS3231_I2C, I2C_FLAG_BUSY)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_GenerateSTART(DS3231_I2C, ENABLE);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_Send7bitAddress(DS3231_I2C, DS3231_ADDRESS, I2C_Direction_Transmitter);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    I2C_SendData(DS3231_I2C, reg);
    timeout = DS3231_TIMEOUT;
    while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
    }

    for(int i=0; i<count; i++) {
        I2C_SendData(DS3231_I2C, buf[i]);
        timeout = DS3231_TIMEOUT;
        while(!I2C_CheckEvent(DS3231_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
            if((timeout--) == 0) { DS3231_I2C_Error = 1; return; }
        }
    }
    
    I2C_GenerateSTOP(DS3231_I2C, ENABLE);
}


void DS3231_Init(RTC_TimeTypeDef *rtc_time) {
    rtc_time->seconds    = 50;
    rtc_time->minutes    = 59;   // 여기에 실제 분
    rtc_time->hours      = 8;   // 여기에 실제 시 (24시간제)
    rtc_time->dayofweek  = 3;    // 1~7 아무거나
    rtc_time->dayofmonth = 10;   // 날짜
    rtc_time->month      = 12;   // 월
    rtc_time->year       = 25;   // 2024 -> 24
}

void DS3231_GetTime(RTC_TimeTypeDef *rtc_time) {
    uint8_t buffer[7];
    DS3231_ReadBurst(0x00, buffer, 7);

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

    DS3231_WriteBurst(0x00, buffer, 7);
}

void DS3231_ClearAlarmFlags(void) {
    uint8_t status;
    status = DS3231_ReadReg(DS3231_STATUS_REG);
    status &= ~0x03; // Clear A1F and A2F
    DS3231_WriteReg(DS3231_STATUS_REG, status);
}

void DS3231_SetAlarm1(uint8_t hour, uint8_t min, uint8_t sec) {
    uint8_t buff[4];
    buff[0] = decToBcd(sec);       
    buff[1] = decToBcd(min);       
    buff[2] = decToBcd(hour);      
    buff[3] = 0x80;                // Day/Date ignored -> Daily match
    
    DS3231_WriteBurst(DS3231_ALARM1_ADDR, buff, 4);
    
    // Enable Interrupt
    uint8_t ctrl = DS3231_ReadReg(DS3231_CONTROL_REG);
    ctrl |= 0x05; // INTCN | A1IE
    DS3231_WriteReg(DS3231_CONTROL_REG, ctrl);
}

void DS3231_SetAlarm2(uint8_t hour, uint8_t min) {
    uint8_t buff[3];
    buff[0] = decToBcd(min);
    buff[1] = decToBcd(hour);
    buff[2] = 0x80;

    DS3231_WriteBurst(DS3231_ALARM2_ADDR, buff, 3);

    uint8_t ctrl = DS3231_ReadReg(DS3231_CONTROL_REG);
    ctrl |= 0x06;   // INTCN | A2IE
    DS3231_WriteReg(DS3231_CONTROL_REG, ctrl);
}

uint8_t DS3231_GetI2CError(void) {
    return DS3231_I2C_Error;
}
