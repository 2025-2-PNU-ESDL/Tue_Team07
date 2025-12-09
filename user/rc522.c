/* Core/Src/rc522.c */
#include "rc522.h"
#include "spi.h" // CubeMX가 만든 spi.h 포함
#include "gpio.h"

// 사용하는 SPI 핸들러 (CubeMX 설정에 따라 hspi1 또는 hspi2)
extern SPI_HandleTypeDef hspi1;

// CS(SS) 핀 설정 (CubeMX에서 PA4를 CS로 설정했다고 가정)
#define RC522_CS_GPIO GPIOA
#define RC522_CS_PIN  GPIO_PIN_4

// 매크로 함수
#define RC522_CS_LOW  HAL_GPIO_WritePin(RC522_CS_GPIO, RC522_CS_PIN, GPIO_PIN_RESET)
#define RC522_CS_HIGH HAL_GPIO_WritePin(RC522_CS_GPIO, RC522_CS_PIN, GPIO_PIN_SET)

// 내부 함수: 레지스터 쓰기
void MFRC522_WriteRegister(uint8_t addr, uint8_t val) {
    uint8_t data[2];
    data[0] = (addr << 1) & 0x7E;
    data[1] = val;
    
    RC522_CS_LOW;
    HAL_SPI_Transmit(&hspi1, data, 2, 100);
    RC522_CS_HIGH;
}

// 내부 함수: 레지스터 읽기
uint8_t MFRC522_ReadRegister(uint8_t addr) {
    uint8_t tx_data = ((addr << 1) & 0x7E) | 0x80;
    uint8_t rx_data;
    
    RC522_CS_LOW;
    HAL_SPI_Transmit(&hspi1, &tx_data, 1, 100);
    HAL_SPI_Receive(&hspi1, &rx_data, 1, 100);
    RC522_CS_HIGH;
    
    return rx_data;
}

// 비트 마스크 설정
void MFRC522_SetBitMask(uint8_t reg, uint8_t mask) {
    uint8_t tmp;
    tmp = MFRC522_ReadRegister(reg);
    MFRC522_WriteRegister(reg, tmp | mask);
}

// 비트 마스크 해제
void MFRC522_ClearBitMask(uint8_t reg, uint8_t mask) {
    uint8_t tmp;
    tmp = MFRC522_ReadRegister(reg);
    MFRC522_WriteRegister(reg, tmp & (~mask));
}

// 안테나 켜기
void MFRC522_AntennaOn(void) {
    uint8_t temp;
    temp = MFRC522_ReadRegister(MFRC522_REG_TX_CONTROL);
    if (!(temp & 0x03)) {
        MFRC522_SetBitMask(MFRC522_REG_TX_CONTROL, 0x03);
    }
}

// 안테나 끄기
void MFRC522_AntennaOff(void) {
    MFRC522_ClearBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

// 초기화 함수
void MFRC522_Init(void) {
    MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_RESETPHASE); // 소프트 리셋
    HAL_Delay(10); // 리셋 대기
    
    MFRC522_WriteRegister(MFRC522_REG_T_MODE, 0x8D);
    MFRC522_WriteRegister(MFRC522_REG_T_PRESCALER, 0x3E);
    MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_L, 30);
    MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_H, 0);
    MFRC522_WriteRegister(MFRC522_REG_TX_ASK, 0x40);
    MFRC522_WriteRegister(MFRC522_REG_MODE, 0x3D);
    
    MFRC522_AntennaOn();
}

// 카드 감지 (Request)
uint8_t MFRC522_Request(uint8_t reqMode, uint8_t *TagType) {
    uint8_t status;
    uint16_t backBits;
    
    MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x07);
    TagType[0] = reqMode;
    
    status = MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);
    
    if ((status != MI_OK) || (backBits != 0x10)) {
        status = MI_ERR;
    }
    return status;
}

// 카드 통신 (ToCard)
uint8_t MFRC522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen) {
    uint8_t status = MI_ERR;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint16_t i;
    
    switch (command) {
        case PCD_AUTHENT:
            irqEn = 0x12;
            waitIRq = 0x10;
            break;
        case PCD_TRANSCEIVE:
            irqEn = 0x77;
            waitIRq = 0x30;
            break;
        default:
            break;
    }
    
    MFRC522_WriteRegister(MFRC522_REG_COMM_IEN, irqEn | 0x80);
    MFRC522_ClearBitMask(MFRC522_REG_COMM_IRQ, 0x80);
    MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);
    MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_IDLE);
    
    for (i = 0; i < sendLen; i++) {
        MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, sendData[i]);
    }
    
    MFRC522_WriteRegister(MFRC522_REG_COMMAND, command);
    if (command == PCD_TRANSCEIVE) {
        MFRC522_SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80);
    }
    
    i = 2000;
    do {
        n = MFRC522_ReadRegister(MFRC522_REG_COMM_IRQ);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));
    
    MFRC522_ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);
    
    if (i != 0) {
        if (!(MFRC522_ReadRegister(MFRC522_REG_ERROR) & 0x1B)) {
            status = MI_OK;
            if (n & irqEn & 0x01) {
                status = MI_NOTAGERR;
            }
            if (command == PCD_TRANSCEIVE) {
                n = MFRC522_ReadRegister(MFRC522_REG_FIFO_LEVEL);
                lastBits = MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;
                if (lastBits) {
                    *backLen = (n - 1) * 8 + lastBits;
                } else {
                    *backLen = n * 8;
                }
                if (n == 0) {
                    n = 1;
                }
                if (n > 16) { // Buffer size check
                    n = 16;
                }
                for (i = 0; i < n; i++) {
                    backData[i] = MFRC522_ReadRegister(MFRC522_REG_FIFO_DATA);
                }
            }
        } else {
            status = MI_ERR;
        }
    }
    return status;
}

// 충돌 방지 (Anticollision) - UID 가져오기
uint8_t MFRC522_Anticoll(uint8_t *SerNum) {
    uint8_t status;
    uint8_t i;
    uint8_t serNumCheck = 0;
    uint16_t unLen;
    
    MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x00);
    SerNum[0] = PICC_ANTICOLL;
    SerNum[1] = 0x20;
    status = MFRC522_ToCard(PCD_TRANSCEIVE, SerNum, 2, SerNum, &unLen);
    
    if (status == MI_OK) {
        for (i = 0; i < 4; i++) {
            serNumCheck ^= SerNum[i];
        }
        if (serNumCheck != SerNum[4]) {
            status = MI_ERR;
        }
    }
    return status;
}

void MFRC522_Halt(void) {
    uint16_t unLen;
    uint8_t buff[4];
    buff[0] = PICC_HALT;
    buff[1] = 0;
    MFRC522_ToCard(PCD_TRANSCEIVE, buff, 2, buff, &unLen);
}

// --- 사용자가 메인에서 호출할 통합 함수 ---
uint8_t RC522_Check(uint8_t *id) {
    uint8_t status;
    status = MFRC522_Request(PICC_REQIDL, id); // 1. 카드 있나 확인
    if (status == MI_OK) {
        status = MFRC522_Anticoll(id); // 2. 있으면 UID 읽기
    }
    MFRC522_Halt(); // 3. 통신 종료 (다음 인식을 위해)
    return status;
}