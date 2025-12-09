#include "stm32f10x.h"
#include "rc522.h"
#include "ds3231.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* --- Global Variables --- */
volatile uint8_t rfid_irq_flag = 0;  // RFID Interrupt Flag
volatile uint8_t rtc_alarm_flag = 0; // RTC Alarm Flag
volatile uint8_t uart_cmd_flag = 0;  // UART Command Flag
volatile uint8_t uart_rx_data;       // UART Rx Data
uint8_t system_active = 0;           // 0: Idle, 1: Active
__IO uint32_t TimingDelay;           // For Delay function

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

/* --- SysTick Handler --- */
void SysTick_Handler(void)
{
    if (TimingDelay != 0x00)
    {
        TimingDelay--;
    }
}

/* --- Main Function --- */
int main(void)
{
    // System Init
    SystemInit();

    // Configure Peripherals
    RCC_Configuration();
    GPIO_Configuration();
    NVIC_Configuration();
    USART_Configuration();
    I2C_Configuration(); // For DS3231 & SSD1306 (if I2C)
    SPI_Configuration(); // For RC522

    // SysTick Config (1ms)
    if (SysTick_Config(SystemCoreClock / 1000))
    {
        while (1);
    }
    
    DS3231_ResetI2CError();

    // Module Init
    LCD_Init();
    DS3231_Init(&sTime);
    DS3231_SetTime(&sTime);
    
    MFRC522_Init();

    // RTC Alarm Setup (Example: 09:00:00)
    DS3231_SetAlarm1(9, 0, 0);
    DS3231_SetAlarm2(9, 2);
    DS3231_ClearAlarmFlags();

    // I2C 에러 확인
    char msg[64];
    sprintf(msg, "After SetTime: err=%d\r\n", DS3231_GetI2CError());
    for (int k = 0; msg[k]; k++) {
        USART_SendData(USART1, msg[k]);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    }

    // Initial Screen
    Display_Idle_Screen();

    // Power-on beep
    Beep(3);

    static uint8_t prev_sec = 0xFF; 

    while (1)
    {
        // 0. Continuous Time Update (Non-blocking)
        // This ensures time updates even during active polling, without Delay(1000) blocking
        DS3231_GetTime(&sTime);
        if (sTime.seconds != prev_sec) {
            prev_sec = sTime.seconds;
            char time_str[20];
            sprintf(time_str, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
            LCD_ShowString(30, 130, (uint8_t*)time_str, BLACK, WHITE);
        }

        // A. RTC Alarm Handling
        if (rtc_alarm_flag) {
            rtc_alarm_flag = 0;
            DS3231_ClearAlarmFlags();

            DS3231_GetTime(&sTime);

            // Start Attendance (09:00)
            if (sTime.hours == 9 && sTime.minutes == 0) {
                system_active = 1;

                char msg[] = "[ATTENDANCE OPEN]\r\n";
                for (int k = 0; k < (int)strlen(msg); k++) {
                    USART_SendData(USART1, msg[k]);
                    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                }
            }
            // End Attendance (10:00)
            if (sTime.hours == 9 && sTime.minutes == 2) {
                system_active = 0;
                Display_Idle_Screen();
                Beep(3);

                char msg[] = "[ATTENDANCE CLOSED]\r\n";
                for (int k = 0; k < (int)strlen(msg); k++) {
                    USART_SendData(USART1, msg[k]);
                    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                }
            }
        }

        // B. RFID Handling (IRQ 안 쓰고 폴링 방식)
        if (system_active) {
            uint8_t uid[5];

            if (RC522_Check(uid) == MI_OK) {

                int user_idx = -1;
                int db_size = sizeof(db) / sizeof(db[0]);

                // UID 문자열 (연속 HEX, 예: 1234ABCD)
                char uid_str[16];
                sprintf(uid_str, "%02X%02X%02X%02X",
                        uid[0], uid[1], uid[2], uid[3]);

                for (int i = 0; i < db_size; i++) {
                    if (memcmp(db[i].uid, uid, 4) == 0) {
                        user_idx = i;
                        break;
                    }
                }

                DS3231_GetTime(&sTime);

                if (user_idx != -1) {
                    char status[10];

                    // LCD Display
                    LCD_Clear(WHITE);
                    LCD_ShowString(20, 20, (uint8_t*)db[user_idx].name, BLACK, WHITE);
                    
                    char time_str[20];
                    sprintf(time_str, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
                    LCD_ShowString(20, 50, (uint8_t*)time_str, BLACK, WHITE);

                    // Late logic
                    if (sTime.hours >= 9 && sTime.minutes >= 1) {
                        strcpy(status, "LATE");
                        GPIO_SetBits(GPIOB, GPIO_Pin_0); // Red LED
                        Beep(2);
                        LCD_ShowString(20, 80, (uint8_t*)"Status: LATE", RED, WHITE);
                    } else {
                        strcpy(status, "OK");
                        GPIO_SetBits(GPIOB, GPIO_Pin_1); // Green LED
                        Beep(1);
                        LCD_ShowString(20, 80, (uint8_t*)"Status: OK", GREEN, WHITE);
                    }

                    // UART 로그: Name, UID, Time, Status
                    char uart_buff[80];
                    sprintf(uart_buff, "%s,%s,%02d:%02d,%s\r\n",
                            db[user_idx].name, uid_str,
                            sTime.hours, sTime.minutes, status);

                    for (int k = 0; k < (int)strlen(uart_buff); k++) {
                        USART_SendData(USART1, uart_buff[k]);
                        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                    }

                } else {
                    // 미등록 태그
                    Beep(1);
                    GPIO_SetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1); // Yellow

                    LCD_Clear(WHITE);
                    LCD_ShowString(20, 20, (uint8_t*)"UNKNOWN TAG", RED, WHITE);
                    char uid_disp[20];
                    sprintf(uid_disp, "UID: %s", uid_str);
                    LCD_ShowString(20, 50, (uint8_t*)uid_disp, BLACK, WHITE);

                    // UART 로그: UNKNOWN,UID,Time
                    char uart_buff[80];
                    sprintf(uart_buff, "UNKNOWN,%s,%02d:%02d\r\n",
                            uid_str, sTime.hours, sTime.minutes);

                    for (int k = 0; k < (int)strlen(uart_buff); k++) {
                        USART_SendData(USART1, uart_buff[k]);
                        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                    }
                }

                // 잠깐 LED 켰다가 끄기
                Delay(500);
                GPIO_ResetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1);
                
                // Return to Idle Screen
                Delay(1000);
                Display_Idle_Screen();
            }

            // 너무 빡세게 폴링하지 않게 약간 쉬어주기
            Delay(50);
        }

        // C. UART Handling (PC -> USART1 ���� ó��)
        if (uart_cmd_flag) {
            uart_cmd_flag = 0;

            switch (uart_rx_data) {
                case 'R':   // RESET / TEST
                    Beep(2);
                    break;

                case 'E':   // EXPORT
                {
                    char msg[] = "EXPORT NOT IMPLEMENTED\r\n";
                    for (int k = 0; k < (int)strlen(msg); k++) {
                        USART_SendData(USART1, msg[k]);
                        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                    }
                    break;
                }

                case 'S':   // TOGGLE ACTIVE
                    system_active = !system_active;
                
                    char active_msg[] = "ACTIVE\r\n";
                    char deactive_msg[] = "DEACTIVE\r\n";

                    if (system_active) {
                        for (int k = 0; k < (int)strlen(active_msg); k++) {
                            USART_SendData(USART1, active_msg[k]);
                            while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                        }
                    } else {
                        for (int k = 0; k < (int)strlen(deactive_msg); k++) {
                            USART_SendData(USART1, deactive_msg[k]);
                            while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                        }
                    }
                    break;

                default:
                    // Unknown command -> ����
                    break;
            }
        }
        
    }
}

/* --- Helper Functions --- */

void Beep(int count)
{
    for (int i = 0; i < count; i++) {
        // Active-low buzzer on PA3
        GPIO_ResetBits(GPIOA, GPIO_Pin_3); // LOW = ON
        Delay(100);
        GPIO_SetBits(GPIOA, GPIO_Pin_3);   // HIGH = OFF
        Delay(100);
    }
}

void Display_Idle_Screen(void)
{
    DS3231_ResetI2CError();
    DS3231_GetTime(&sTime);

    LCD_Clear(WHITE);
    LCD_ShowString(30, 100, (uint8_t*)"READY", BLUE, WHITE);
    
    // Initial time display.
    char time_str[20];
    sprintf(time_str, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
    LCD_ShowString(30, 130, (uint8_t*)time_str, BLACK, WHITE);

    // "SYSTEM IDLE" + 현재 시각 로그로 출력 (for UART debugging)
    char msg[64];
    sprintf(msg, "SYSTEM IDLE %02d:%02d:%02d (err=%d)\r\n",
            sTime.hours, sTime.minutes, sTime.seconds, DS3231_GetI2CError());

    for (int k = 0; k < (int)strlen(msg); k++) {
        USART_SendData(USART1, msg[k]);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    }
}

void Delay(__IO uint32_t nTime)
{
    TimingDelay = nTime;
    while (TimingDelay != 0);
}

/* --- Configuration Functions --- */

void RCC_Configuration(void)
{
    /* APB2: GPIO, AFIO, USART1, SPI1 */
    RCC_APB2PeriphClockCmd(
        RCC_APB2Periph_GPIOA |
        RCC_APB2Periph_GPIOB |
        RCC_APB2Periph_GPIOD |
        RCC_APB2Periph_AFIO  |
        RCC_APB2Periph_USART1|
        RCC_APB2Periph_SPI1,
        ENABLE
    );

    /* APB1: I2C1, USART2 */
    RCC_APB1PeriphClockCmd(
        RCC_APB1Periph_I2C1  |
        RCC_APB1Periph_USART2,
        ENABLE
    );
}

void GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* USART1 Tx (PA9) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART1 Rx (PA10) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // or IN_FLOATING
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART2 Remap: PD5(TX), PD6(RX) */
    GPIO_PinRemapConfig(GPIO_Remap_USART2, ENABLE);

    /* USART2 Tx (PD5) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* USART2 Rx (PD6) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    /* SPI1 SCK (PA5), MOSI (PA7) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* SPI1 MISO (PA6) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RC522 CS (PA4) - Software Control */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* I2C1 SCL (PB6), SDA (PB7) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* Buzzer (PA3, Active-Low) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_3); // HIGH = OFF

    /* LEDs (PB0, PB1) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* RFID IRQ (PA0) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RTC INT (PA1) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void USART_Configuration(void)
{
    USART_InitTypeDef USART_InitStructure;

    /* --- USART1 (PC) --- */
    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);

    // Enable USART1 RX interrupt
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* --- USART2 (Bluetooth) --- */
    USART_InitStructure.USART_BaudRate            = 9600;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART2, &USART_InitStructure);
    USART_Cmd(USART2, ENABLE);

    // �ʿ��ϸ� �������� RX�� ���
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
}

void I2C_Configuration(void)
{
    I2C_InitTypeDef  I2C_InitStructure;

    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle           = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1         = 0x00;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed          = 100000; // 100kHz

    I2C_Init(I2C1, &I2C_InitStructure);
    I2C_Cmd(I2C1, ENABLE);
}

void SPI_Configuration(void)
{
    SPI_InitTypeDef  SPI_InitStructure;

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;

    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
}

void NVIC_Configuration(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;

    // Priority Group
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* USART1 IRQ */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* USART2 IRQ */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 2;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* EXTI Line0 (PA0) - RFID IRQ */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    EXTI_InitStructure.EXTI_Line    = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_Init(&NVIC_InitStructure);

    /* EXTI Line1 (PA1) - RTC Alarm */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);
    EXTI_InitStructure.EXTI_Line    = EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = EXTI1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_Init(&NVIC_InitStructure);
}

/* --- Interrupt Handlers --- */

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        uint8_t data = USART_ReceiveData(USART1);

        // PC���� �� ������ -> ���������ε� ������ ������
        USART_SendData(USART2, data);
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);

        // ���� ó���� ���� ������ ����
        uart_rx_data = data;
        uart_cmd_flag = 1;

        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t data = USART_ReceiveData(USART2);

        // ������������ �� ������ -> PC�� ����
        USART_SendData(USART1, data);
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);

        // ���⼭�� uart_rx_data / uart_cmd_flag �ǵ帮�� ����
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

void EXTI0_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        rfid_irq_flag = 1;
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

void EXTI1_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        rtc_alarm_flag = 1;
        EXTI_ClearITPendingBit(EXTI_Line1);
    }
}
