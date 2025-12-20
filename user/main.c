#include "stm32f10x.h"
#include "rc522.h"
#include "ds3231.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* --- Global Variables --- */
volatile uint8_t rfid_irq_flag = 0;
volatile uint8_t rtc_alarm_flag = 0;
volatile uint8_t uart_cmd_flag = 0;
volatile uint8_t uart_rx_data;
uint8_t system_active = 0;

/* 원래 방식대로 복구 */
__IO uint32_t TimingDelay = 0;

RTC_TimeTypeDef sTime;
char str_buff[64];

/* --- Student DB --- */
struct {
    uint8_t uid[4];
    char name[10];
} db[] = {
    {{0x1C, 0x43, 0x6D, 0x06}, "LeeNY"},
    {{0x9B, 0x81, 0x4D, 0x06}, "SeungWoo"},
    {{0xC9, 0xD4, 0x6B, 0x06}, "Andrea"}
};

/* --- Function Prototypes --- */
void RCC_Configuration(void);
void GPIO_Configuration(void);
void NVIC_Configuration(void);
void USART_Configuration(void);
void I2C_Configuration(void);
void SPI_Configuration(void);
void Delay(__IO uint32_t nTime);
void Beep(int count);
void Display_Idle_Screen(void);
void Send_UART_Msg(USART_TypeDef* USARTx, char* msg);
void I2C_ResetBus(void);

/* --- SysTick Handler (원래 코드로 복구) --- */
void SysTick_Handler(void) {
    if (TimingDelay != 0x00) {
        TimingDelay--;
    }
}

/* --- Main Function --- */
int main(void)
{
    /* [중요] 변수 선언을 무조건 맨 위로 올려서 에러 방지 */
    static uint8_t prev_sec = 0xFF; 
    uint8_t uid[5];
    int user_idx;
    int db_count;
    int i;
    char status[10];
    char uart_buff[80];
    char uid_str[16];
    char time_str[20];
    char time_disp[20];

    SystemInit();

    // 1. I2C 버스 클리어
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    I2C_ResetBus(); 

    RCC_Configuration();
    GPIO_Configuration();
    NVIC_Configuration();
    USART_Configuration();
    I2C_Configuration(); 
    SPI_Configuration(); 

    /* SysTick 1ms 설정 */
    if (SysTick_Config(SystemCoreClock / 1000)) { 
        while (1); 
    }
    
    DS3231_ResetI2CError();

    LCD_Init();
    MFRC522_Init();
    DS3231_Init(&sTime);
    
    // 알람 설정
    DS3231_SetAlarm1(9, 0, 0);
    DS3231_SetAlarm2(9, 2);
    DS3231_ClearAlarmFlags();

    // [요청사항] 부팅 메시지 -> UART2 (블루투스)
    Send_UART_Msg(USART2, "System Ready (Via BT, Buzzer@PA3)\r\n");

    Display_Idle_Screen();
    Beep(1); // 부팅 비프음 (PA3)

    while (1)
    {
        DS3231_GetTime(&sTime);
        
        if (sTime.seconds != prev_sec) {
            prev_sec = sTime.seconds;
            sprintf(time_str, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
            LCD_ShowString(30, 130, (uint8_t*)time_str, BLACK, WHITE);
        }

        // A. RTC Alarm Logic
        if (rtc_alarm_flag) {
            rtc_alarm_flag = 0;
            DS3231_ClearAlarmFlags();
            DS3231_GetTime(&sTime);

            if (sTime.hours == 9 && sTime.minutes == 0) {
                if(!system_active) {
                    system_active = 1;
                    Send_UART_Msg(USART2, "[ATTENDANCE OPEN]\r\n");
                }
            }
            if (sTime.hours == 9 && sTime.minutes == 2) {
                if(system_active) {
                    system_active = 0;
                    Display_Idle_Screen();
                    Beep(3);
                    Send_UART_Msg(USART2, "[ATTENDANCE CLOSED]\r\n");
                }
            }
        }

        // B. RFID Handling
        if (system_active) {
            if (RC522_Check(uid) == MI_OK) {
                user_idx = -1;
                db_count = sizeof(db) / sizeof(db[0]);
                
                // [수정] 변수 i는 맨 위에서 선언했음
                for (i = 0; i < db_count; i++) {
                    if (memcmp(db[i].uid, uid, 4) == 0) {
                        user_idx = i; break;
                    }
                }
                
                DS3231_GetTime(&sTime);
                sprintf(uid_str, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);

                LCD_Clear(WHITE);
                
                if (user_idx != -1) {
                    LCD_ShowString(20, 20, (uint8_t*)db[user_idx].name, BLACK, WHITE);
                    sprintf(time_disp, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
                    LCD_ShowString(20, 50, (uint8_t*)time_disp, BLACK, WHITE);

                    if (sTime.hours >= 9 && sTime.minutes >= 1) {
                        strcpy(status, "LATE");
                        LCD_ShowString(20, 80, (uint8_t*)"Status: LATE", RED, WHITE);
                        GPIO_SetBits(GPIOB, GPIO_Pin_0); 
                        Beep(2);
                    } else {
                        strcpy(status, "OK");
                        LCD_ShowString(20, 80, (uint8_t*)"Status: OK", GREEN, WHITE);
                        GPIO_SetBits(GPIOB, GPIO_Pin_1); 
                        Beep(1);
                    }
                    sprintf(uart_buff, "%s,%s,%02d:%02d,%s\r\n", db[user_idx].name, uid_str, sTime.hours, sTime.minutes, status);
                } else {
                    LCD_ShowString(20, 20, (uint8_t*)"UNKNOWN TAG", RED, WHITE);
                    LCD_ShowString(20, 50, (uint8_t*)uid_str, BLACK, WHITE);
                    GPIO_SetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1);
                    Beep(1);
                    sprintf(uart_buff, "UNKNOWN,%s,%02d:%02d\r\n", uid_str, sTime.hours, sTime.minutes);
                }
                
                // [요청사항] UART2로만 전송
                Send_UART_Msg(USART2, uart_buff);
                
                Delay(500);
                GPIO_ResetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1);
                Delay(500);
                Display_Idle_Screen();
            }
        }

        // C. UART Commands (블루투스 전용)
        if (uart_cmd_flag) {
            uart_cmd_flag = 0;
            // 에코
            USART_SendData(USART2, uart_rx_data); 
            
            if(uart_rx_data == 'S') {
                system_active = !system_active;
                if(system_active) Send_UART_Msg(USART2, "ACTIVE\r\n");
                else Send_UART_Msg(USART2, "DEACTIVE\r\n");
                Display_Idle_Screen();
            } else if (uart_rx_data == 'R') {
                 Beep(1);
                 Send_UART_Msg(USART2, "RESET OK\r\n");
            }
        }
        Delay(50);
    }
}

/* --- Helper Functions --- */
void I2C_ResetBus(void) {
    GPIO_InitTypeDef GPIO_InitStructure;
    int i; // [수정] 변수 선언 맨 위로

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    for(i=0; i<10; i++) {
        GPIO_ResetBits(GPIOB, GPIO_Pin_6); Delay(1);
        GPIO_SetBits(GPIOB, GPIO_Pin_6);   Delay(1);
    }
    GPIO_ResetBits(GPIOB, GPIO_Pin_7); Delay(1);
    GPIO_SetBits(GPIOB, GPIO_Pin_6);   Delay(1);
    GPIO_SetBits(GPIOB, GPIO_Pin_7);   
}

void Send_UART_Msg(USART_TypeDef* USARTx, char* msg) {
    while (*msg) {
        USART_SendData(USARTx, *msg++);
        while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET);
    }
}

// [요청사항] 부저 PA3
void Beep(int count) {
    int i; // [수정] 변수 선언 맨 위로
    for (i = 0; i < count; i++) {
        GPIO_ResetBits(GPIOA, GPIO_Pin_3); Delay(80); 
        GPIO_SetBits(GPIOA, GPIO_Pin_3);   Delay(80); 
    }
}

void Display_Idle_Screen(void) {
    char time_str[20]; // [수정] 변수 선언 맨 위로
    LCD_Clear(WHITE);
    if(system_active) LCD_ShowString(30, 100, (uint8_t*)"SCAN TAG...", RED, WHITE);
    else LCD_ShowString(30, 100, (uint8_t*)"SYSTEM IDLE", BLUE, WHITE);
    DS3231_GetTime(&sTime);
    sprintf(time_str, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
    LCD_ShowString(30, 130, (uint8_t*)time_str, BLACK, WHITE);
}

void Delay(__IO uint32_t nTime) {
    TimingDelay = nTime;
    while (TimingDelay != 0);
}

/* --- Configurations --- */
void RCC_Configuration(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO | RCC_APB2Periph_USART1 | RCC_APB2Periph_SPI1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1 | RCC_APB1Periph_USART2, ENABLE);
}

void GPIO_Configuration(void) {
    GPIO_InitTypeDef GPIO_InitStructure;

    // USART1 (설정만 유지)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // USART2 Remap -> PD5(TX), PD6(RX) (F107VC 전용)
    GPIO_PinRemapConfig(GPIO_Remap_USART2, ENABLE); 
    
    // PD5 (TX)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // PD6 (RX)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // SPI1 (RFID)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // I2C1 (DS3231)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // [요청사항 준수] Buzzer -> PA3
    // (PD5, PD6로 리맵했으므로 PA3는 GPIO Output으로 사용 가능)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_3); // OFF (HIGH=OFF 가정)

    // LEDs
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // IRQs
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void USART_Configuration(void) {
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 9600; 
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    USART_Init(USART2, &USART_InitStructure);
    USART_Cmd(USART2, ENABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
}

void I2C_Configuration(void) {
    I2C_InitTypeDef I2C_InitStructure;
    I2C_Cmd(I2C1, ENABLE);
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_Init(I2C1, &I2C_InitStructure);
}

void SPI_Configuration(void) {
    SPI_InitTypeDef SPI_InitStructure;
    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
}

void NVIC_Configuration(void) {
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_Init(&NVIC_InitStructure);
    EXTI_InitTypeDef EXTI_InitStructure;
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);
}

void USART1_IRQHandler(void) {
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
        USART_ReceiveData(USART1); // 데이터 버림
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void USART2_IRQHandler(void) {
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uart_rx_data = USART_ReceiveData(USART2);
        uart_cmd_flag = 1;
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

void EXTI0_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        rfid_irq_flag = 1; EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

void EXTI1_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        rtc_alarm_flag = 1; EXTI_ClearITPendingBit(EXTI_Line1);
    }
}