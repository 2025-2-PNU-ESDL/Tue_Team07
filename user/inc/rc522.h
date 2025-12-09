/* Core/Inc/rc522.h */
#ifndef __RC522_H
#define __RC522_H

#include "main.h"

// MFRC522 레지스터 정의
#define MFRC522_REG_COMMAND             0x01
#define MFRC522_REG_COMM_IEN            0x02
#define MFRC522_REG_DIV_IEN             0x03
#define MFRC522_REG_COMM_IRQ            0x04
#define MFRC522_REG_DIV_IRQ             0x05
#define MFRC522_REG_ERROR               0x06
#define MFRC522_REG_STATUS1             0x07
#define MFRC522_REG_STATUS2             0x08
#define MFRC522_REG_FIFO_DATA           0x09
#define MFRC522_REG_FIFO_LEVEL          0x0A
#define MFRC522_REG_WATER_LEVEL         0x0B
#define MFRC522_REG_CONTROL             0x0C
#define MFRC522_REG_BIT_FRAMING         0x0D
#define MFRC522_REG_COLL                0x0E
#define MFRC522_REG_MODE                0x11
#define MFRC522_REG_TX_MODE             0x12
#define MFRC522_REG_RX_MODE             0x13
#define MFRC522_REG_TX_CONTROL          0x14
#define MFRC522_REG_TX_ASK              0x15
#define MFRC522_REG_TX_SEL              0x16
#define MFRC522_REG_RX_SEL              0x17
#define MFRC522_REG_RX_THRESHOLD        0x18
#define MFRC522_REG_DEMOD               0x19
#define MFRC522_REG_MF_TX               0x1C
#define MFRC522_REG_MF_RX               0x1D
#define MFRC522_REG_SERIAL_SPEED        0x1F
#define MFRC522_REG_CRC_RESULT_M        0x21
#define MFRC522_REG_CRC_RESULT_L        0x22
#define MFRC522_REG_MOD_WIDTH           0x24
#define MFRC522_REG_RFCFG               0x26
#define MFRC522_REG_GS_N                0x27
#define MFRC522_REG_CW_GS_P             0x28
#define MFRC522_REG_MODGS_P             0x29
#define MFRC522_REG_T_MODE              0x2A
#define MFRC522_REG_T_PRESCALER         0x2B
#define MFRC522_REG_T_RELOAD_H          0x2C
#define MFRC522_REG_T_RELOAD_L          0x2D
#define MFRC522_REG_T_COUNTER_VALUE_H   0x2E
#define MFRC522_REG_T_COUNTER_VALUE_L   0x2F
#define MFRC522_REG_TEST_SEL1           0x31
#define MFRC522_REG_TEST_SEL2           0x32
#define MFRC522_REG_TEST_PIN_EN         0x33
#define MFRC522_REG_TEST_PIN_VALUE      0x34
#define MFRC522_REG_TEST_BUS            0x35
#define MFRC522_REG_AUTO_TEST           0x36
#define MFRC522_REG_VERSION             0x37
#define MFRC522_REG_ANALOG_TEST         0x38
#define MFRC522_REG_TEST_DAC1           0x39
#define MFRC522_REG_TEST_DAC2           0x3A
#define MFRC522_REG_TEST_ADC            0x3B

// MFRC522 명령어
#define PCD_IDLE                        0x00
#define PCD_AUTHENT                     0x0E
#define PCD_RECEIVE                     0x08
#define PCD_TRANSMIT                    0x04
#define PCD_TRANSCEIVE                  0x0C
#define PCD_RESETPHASE                  0x0F
#define PCD_CALCCRC                     0x03

// Mifare_One 카드 명령어
#define PICC_REQIDL                     0x26
#define PICC_REQALL                     0x52
#define PICC_ANTICOLL                   0x93
#define PICC_SElECTTAG                  0x93
#define PICC_AUTHENT1A                  0x60
#define PICC_AUTHENT1B                  0x61
#define PICC_READ                       0x30
#define PICC_WRITE                      0xA0
#define PICC_DECREMENT                  0xC0
#define PICC_INCREMENT                  0xC1
#define PICC_RESTORE                    0xC2
#define PICC_TRANSFER                   0xB0
#define PICC_HALT                       0x50

// 상태 코드
#define MI_OK                           0
#define MI_NOTAGERR                     1
#define MI_ERR                          2

// 함수 원형
void MFRC522_Init(void);
uint8_t MFRC522_Request(uint8_t reqMode, uint8_t *TagType);
uint8_t MFRC522_Anticoll(uint8_t *SerNum);
uint8_t MFRC522_SelectTag(uint8_t *SerNum);
uint8_t MFRC522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *SerNum);
uint8_t MFRC522_Read(uint8_t blockAddr, uint8_t *recvData);
uint8_t MFRC522_Write(uint8_t blockAddr, uint8_t *writeData);
void MFRC522_Halt(void);

// 편의 함수 (이걸 메인에서 쓰세요)
uint8_t RC522_Check(uint8_t *id);

#endif