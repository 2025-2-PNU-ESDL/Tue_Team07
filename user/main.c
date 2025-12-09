#include "stm32f10x.h"
#include "rc522.h"
#include "ds3231.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <string.h>

/* --- Global Variables --- */
volatile uint8_t rfid_irq_flag = 0;  // RFID Interrupt Flag
volatile uint8_t rtc_alarm_flag = 0; // RTC Alarm Flag
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
    {{0x12, 0x34, 0x56, 0x78}, "LeeNY"},      
    {{0xAA, 0xBB, 0xCC, 0xDD}, "SeungWoo"},
    {{0xFF, 0xEE, 0xDD, 0xCC}, "Andrea"}
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
void Check_Attendance_Status(void);
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

    // Module Init
    DS3231_Init();
    MFRC522_Init();
    ssd1306_Init();

    // Enable UART Rx Interrupt
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    // RTC Alarm Setup (Example: 09:00:00)
    DS3231_SetAlarm1(9, 0, 0); 
    DS3231_ClearAlarmFlags();

    // Initial Screen
    Display_Idle_Screen();

    while (1)
    {
        // A. RTC Alarm Handling
        if (rtc_alarm_flag) {
            rtc_alarm_flag = 0;
            DS3231_ClearAlarmFlags();

            DS3231_GetTime(&sTime);

            // Start Attendance (09:00)
            if (sTime.hours == 9 && sTime.minutes == 0) {
                system_active = 1;
                ssd1306_Fill(Black);
                ssd1306_SetCursor(10, 20);
                ssd1306_WriteString("ATTENDANCE", Font_7x10, White);
                ssd1306_SetCursor(25, 35);
                ssd1306_WriteString("OPEN!",      Font_11x18, White);
                ssd1306_UpdateScreen();
            }
            // End Attendance (10:00)
            else if (sTime.hours == 10 && sTime.minutes == 0) {
                system_active = 0;
                Display_Idle_Screen();
                Beep(3);
            }
        }

        // B. RFID Handling
        if (system_active && rfid_irq_flag) {
            rfid_irq_flag = 0; 
            uint8_t uid[5];

            if (RC522_Check(uid) == MI_OK) {
                
                int user_idx = -1;
                int db_size = sizeof(db) / sizeof(db[0]);
                
                for(int i=0; i < db_size; i++) {
                    if(memcmp(db[i].uid, uid, 4) == 0) {
                        user_idx = i;
                        break;
                    }
                }

                DS3231_GetTime(&sTime);
                ssd1306_Fill(Black);

                if (user_idx != -1) {
                    char status[10];
                    // Late logic
                    if (sTime.hours > 9 || (sTime.hours == 9 && sTime.minutes > 10)) {
                        strcpy(status, "LATE");
                        GPIO_SetBits(GPIOB, GPIO_Pin_0); // Red LED
                        Beep(2); 
                    } else {
                        strcpy(status, "OK");
                        GPIO_SetBits(GPIOB, GPIO_Pin_1); // Green LED
                        Beep(1);
                    }

                    sprintf(str_buff, "Name: %s", db[user_idx].name);
                    ssd1306_SetCursor(0, 10);
                    ssd1306_WriteString(str_buff, Font_7x10, White);

                    sprintf(str_buff, "%02d:%02d [%s]", sTime.hours, sTime.minutes, status);
                    ssd1306_SetCursor(0, 30);
                    ssd1306_WriteString(str_buff, Font_11x18, White);
                    
                    // UART Transmit
                    char uart_buff[64];
                    sprintf(uart_buff, "%s,%02d:%02d,%s\r\n", db[user_idx].name, sTime.hours, sTime.minutes, status);
                    for(int k=0; k<strlen(uart_buff); k++) {
                        USART_SendData(USART1, uart_buff[k]);
                        while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                    }

                } else {
                    ssd1306_SetCursor(10, 20);
                    ssd1306_WriteString("UNKNOWN TAG", Font_11x18, White);
                    
                    sprintf(str_buff, "%02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
                    ssd1306_SetCursor(0, 30);
                    ssd1306_WriteString(str_buff, Font_11x18, White);

                    char uart_buff[64];
                    sprintf(uart_buff, "UNKNOWN,%02X%02X%02X%02X\r\n", uid[0], uid[1], uid[2], uid[3]);
                    for(int k=0; k<strlen(uart_buff); k++) {
                        USART_SendData(USART1, uart_buff[k]);
                        while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                    }

                    GPIO_SetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1); // Yellow (Red+Green)
                    Beep(1);
                }

                ssd1306_UpdateScreen();
                Delay(2000);
                GPIO_ResetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1);

                ssd1306_Fill(Black);
                ssd1306_SetCursor(10, 20);
                ssd1306_WriteString("ATTENDANCE", Font_7x10, White);
                ssd1306_SetCursor(25, 35);
                ssd1306_WriteString("OPEN!",      Font_11x18, White);
                ssd1306_UpdateScreen();
            }
        }
    }
}

/* --- Helper Functions --- */

void Beep(int count) {
    for (int i = 0; i < count; i++) {
        GPIO_SetBits(GPIOB, GPIO_Pin_5);
        Delay(100);
        GPIO_ResetBits(GPIOB, GPIO_Pin_5);
        Delay(100);
    }
}

void Display_Idle_Screen(void) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(20, 10);
    ssd1306_WriteString("SYSTEM IDLE", Font_7x10, White);
    
    DS3231_GetTime(&sTime);
    sprintf(str_buff, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
    ssd1306_SetCursor(15, 30);
    ssd1306_WriteString(str_buff, Font_11x18, White);
    
    ssd1306_UpdateScreen();
}

void Delay(__IO uint32_t nTime)
{
    TimingDelay = nTime;
    while(TimingDelay != 0);
}

/* --- Configuration Functions --- */

void RCC_Configuration(void)
{
    /* Enable GPIO, AFIO, USART1, SPI1 Clocks */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_AFIO  | RCC_APB2Periph_USART1 |
                           RCC_APB2Periph_SPI1, ENABLE);
    
    /* Enable I2C1 Clock */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
}

void GPIO_Configuration(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* USART1 Tx (PA9) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART1 Rx (PA10) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; // or IPU
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* SPI1 SCK (PA5), MOSI (PA7) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* SPI1 MISO (PA6) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RC522 CS (PA4) - Software Control */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    /* I2C1 SCL (PB6), SDA (PB7) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* Buzzer (PB5) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* LEDs (PB0, PB1) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    /* RFID IRQ (PA0) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* RTC INT (PA1) */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void USART_Configuration(void)
{
    USART_InitTypeDef USART_InitStructure;

    USART_InitStructure.USART_BaudRate = 9600;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART1, &USART_InitStructure);
    USART_Cmd(USART1, ENABLE);
}

void I2C_Configuration(void)
{
    I2C_InitTypeDef  I2C_InitStructure;

    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = 100000; // 100kHz

    I2C_Init(I2C1, &I2C_InitStructure);
    I2C_Cmd(I2C1, ENABLE);
}

void SPI_Configuration(void)
{
    SPI_InitTypeDef  SPI_InitStructure;

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

void NVIC_Configuration(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    /* USART1 IRQ */
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* Configure EXTI Line0 (PA0) */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line = EXTI_Line0;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling; // Check sensor active level
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_Init(&NVIC_InitStructure);

    /* Configure EXTI Line1 (PA1) */
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);
    EXTI_InitStructure.EXTI_Line = EXTI_Line1;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
    NVIC_Init(&NVIC_InitStructure);
}

/* --- Interrupt Handlers --- */

void USART1_IRQHandler(void)
{
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        uart_rx_data = USART_ReceiveData(USART1);
        
        // Command Handling
        switch (uart_rx_data) {
        case 'R':   // RESET
            Beep(2);
            break;

        case 'E':   // EXPORT
            {
                char msg[] = "EXPORT NOT IMPLEMENTED\r\n";
                for(int k=0; k<strlen(msg); k++) {
                    USART_SendData(USART1, msg[k]);
                    while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
                }
            }
            break;

        case 'S':   // SET DEADLINE
            system_active = !system_active;
            break;
        }
    }
}

void EXTI0_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line0) != RESET)
    {
        rfid_irq_flag = 1;
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

void EXTI1_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line1) != RESET)
    {
        rtc_alarm_flag = 1;
        EXTI_ClearITPendingBit(EXTI_Line1);
    }
}
