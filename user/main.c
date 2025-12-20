#include "stm32f10x.h"
#include "rc522.h"
#include "ds3231.h"
#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* --- Global Variables --- */
volatile uint8_t rfid_irq_flag = 0;
volatile uint8_t rtc_alarm_flag = 0;

#define RX_BUFFER_SIZE 64
volatile uint8_t rx_buffer[RX_BUFFER_SIZE];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;

/* [설정] 출석 및 마감 시간 변수 (기본값) */
uint8_t att_hour = 9, att_min = 0, att_sec = 0;
uint8_t late_hour = 9, late_min = 0, late_sec = 10; /* [수정] 초 단위 추가 */
uint8_t dead_hour = 9, dead_min = 0, dead_sec = 20; /* [수정] 초 단위 추가 */
static char cmd_buffer[64];
static uint8_t cmd_len = 0;

uint8_t system_active = 0;

/* [설정] 사용할 UART 포트 선택 (1: USART1/PC, 2: USART2/BT) */
#define UART_SELECT 2

#if UART_SELECT == 1
    #define ACTIVE_USART USART1
#else
    #define ACTIVE_USART USART2
#endif

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

    /* [중요] SysTick을 가장 먼저 초기화해야 Delay 함수가 작동합니다. */
    /* 이 코드가 뒤에 있으면 I2C_ResetBus() 내부의 Delay()에서 무한 루프에 빠집니다. */
    if (SysTick_Config(SystemCoreClock / 1000)) { 
        while (1); 
    }

    // 1. I2C 버스 클리어 (이제 Delay 사용 가능)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    I2C_ResetBus(); 

    RCC_Configuration();
    GPIO_Configuration();
    NVIC_Configuration();
    USART_Configuration();
    I2C_Configuration(); 
    SPI_Configuration(); 

    /* [진단] LCD 초기화 전 비프음: CPU 정상 동작 확인 및 전원 안정화 대기 */
    /* 소리가 나면 CPU는 정상입니다. 소리가 나는데 화면이 안 나오면 LCD 배선을 확인하세요. */
    Beep(1); 
    Delay(200); // LCD 전원 안정화 대기 (중요)
    
    DS3231_ResetI2CError();

    LCD_Init();
    MFRC522_Init();
    DS3231_Init(&sTime);
    DS3231_SetTime(&sTime); /* [수정] 구조체에 설정된 시간을 실제 DS3231 모듈에 전송 */
    
    // 알람 설정
    DS3231_SetAlarm1(att_hour, att_min, att_sec);
    DS3231_SetAlarm2(late_hour, late_min, late_sec); /* [수정] Alarm2는 이제 LATE 시간 */
    DS3231_SetAlarm3(dead_hour, dead_min, dead_sec); /* [추가] Alarm3는 DEADLINE (소프트웨어) */
    DS3231_ClearAlarmFlags();

    // [요청사항] 부팅 메시지 -> UART2 (블루투스)
    Send_UART_Msg(ACTIVE_USART, "System Ready (Via Selected UART)\r\n");

    Display_Idle_Screen();

    while (1)
    {
        DS3231_GetTime(&sTime);
        
        if (sTime.seconds != prev_sec) {
            prev_sec = sTime.seconds;
            sprintf(time_str, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
            LCD_ShowString(30, 130, (uint8_t*)time_str, BLACK, WHITE);

            /* [수정] 시간 기반 이벤트 체크 (초 단위 정밀 제어) - 중복 실행 방지를 위해 초 변경 시 수행 */
            if (system_active) {
                // 1. LATE 시작
                if (sTime.hours == late_hour && sTime.minutes == late_min && sTime.seconds == late_sec) {
                    Beep(2);
                    Send_UART_Msg(ACTIVE_USART, "[LATE PERIOD START]\r\n");
                    Display_Idle_Screen(); /* [추가] 화면 갱신 (ATTENDANCE -> LATE PERIOD) */
                }

                // 2. Deadline (출석 마감)
                if (sTime.hours == dead_hour && sTime.minutes == dead_min && sTime.seconds == dead_sec) {
                    system_active = 0;
                    Display_Idle_Screen(); /* [추가] 화면 갱신 (LATE PERIOD -> CLOSED) */
                    Beep(3);
                    Send_UART_Msg(ACTIVE_USART, "[ATTENDANCE CLOSED]\r\n");
                }
            }
        }

        // A. RTC Alarm Logic
        if (rtc_alarm_flag) {
            rtc_alarm_flag = 0;
            DS3231_ClearAlarmFlags();
            DS3231_GetTime(&sTime);

            if (sTime.hours == att_hour && sTime.minutes == att_min) {
                if(!system_active) {
                    system_active = 1;
                    Beep(1);
                    Send_UART_Msg(ACTIVE_USART, "[ATTENDANCE OPEN]\r\n");
                    Display_Idle_Screen(); /* [수정] 화면 갱신 추가 (SYSTEM IDLE -> SCAN TAG...) */
                }
            }
            /* Alarm 2 (LATE)는 메인 루프 폴링에서 초 단위로 정확하게 처리하므로 여기서는 제외 */
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

                    /* [수정] 지각 판단 로직 (초 단위까지 비교) */
                    if (sTime.hours > late_hour || 
                       (sTime.hours == late_hour && sTime.minutes > late_min) ||
                       (sTime.hours == late_hour && sTime.minutes == late_min && sTime.seconds >= late_sec)) {
                        strcpy(status, "LATE");
                        LCD_ShowString(20, 80, (uint8_t*)"Status: LATE", RED, WHITE);
                        GPIO_ResetBits(GPIOB, GPIO_Pin_0); /* [수정] LED ON (Active Low) */
                        Beep(2);
                    } else {
                        strcpy(status, "OK");
                        LCD_ShowString(20, 80, (uint8_t*)"Status: OK", GREEN, WHITE);
                        GPIO_ResetBits(GPIOB, GPIO_Pin_1); /* [수정] LED ON (Active Low) */
                        Beep(1);
                    }
                    sprintf(uart_buff, "%s,%s,%02d:%02d,%s\r\n", db[user_idx].name, uid_str, sTime.hours, sTime.minutes, status);
                } else {
                    LCD_ShowString(20, 20, (uint8_t*)"UNKNOWN TAG", RED, WHITE);
                    LCD_ShowString(20, 50, (uint8_t*)uid_str, BLACK, WHITE);
                    GPIO_ResetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1); /* [수정] LED ON (Active Low) */
                    Beep(3);
                    sprintf(uart_buff, "UNKNOWN,%s,%02d:%02d\r\n", uid_str, sTime.hours, sTime.minutes);
                }
                
                // [요청사항] UART2로만 전송
                Send_UART_Msg(ACTIVE_USART, uart_buff);
                
                Delay(1000); /* [수정] 1초 유지 */
                GPIO_SetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1); /* [수정] LED OFF */
                Display_Idle_Screen();
            }
        }

        // C. UART Commands (블루투스 전용)
        /* 링 버퍼에 데이터가 있으면 하나씩 꺼내서 처리 */
        while (rx_head != rx_tail) {
            uint8_t data = rx_buffer[rx_tail];
            rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;

            // 에코
            while (USART_GetFlagStatus(ACTIVE_USART, USART_FLAG_TXE) == RESET); // 전송 완료 대기
            USART_SendData(ACTIVE_USART, data); 
            
            /* [수정] 줄 단위 명령어 처리 (SET ATTENDANCE 등 지원) */
            if (data == '\n' || data == '\r') {
                if (cmd_len > 0) {
                    cmd_buffer[cmd_len] = 0; // 문자열 종료
                    
                    if (strcmp(cmd_buffer, "S") == 0) {
                        system_active = !system_active;
                        if(system_active) Send_UART_Msg(ACTIVE_USART, "ACTIVE\r\n");
                        else Send_UART_Msg(ACTIVE_USART, "DEACTIVE\r\n");
                        Display_Idle_Screen();
                    } else if (strcmp(cmd_buffer, "R") == 0) {
                        Beep(1);
                        DS3231_Init(&sTime);
                        DS3231_SetTime(&sTime);
                        
                        /* [수정] 리셋 시 현재 시간이 출석 범위 내인지 확인하여 상태 복구 */
                        uint32_t cur_total = sTime.hours * 3600UL + sTime.minutes * 60UL + sTime.seconds;
                        uint32_t att_total = att_hour * 3600UL + att_min * 60UL + att_sec;
                        uint32_t dead_total = dead_hour * 3600UL + dead_min * 60UL + dead_sec;

                        if (cur_total >= att_total && cur_total < dead_total) {
                            system_active = 1;
                        } else {
                            system_active = 0;
                        }

                        Send_UART_Msg(ACTIVE_USART, "RESET OK\r\n");
                        Display_Idle_Screen(); /* [추가] 리셋 후 화면 갱신 */
                    } else if (strncmp(cmd_buffer, "SET ATTENDANCE ", 15) == 0) {
                        int h, m, s;
                        if (sscanf(cmd_buffer + 15, "%d:%d:%d", &h, &m, &s) == 3) {
                            att_hour = h; att_min = m; att_sec = s;
                            DS3231_SetAlarm1(att_hour, att_min, att_sec);
                            Send_UART_Msg(ACTIVE_USART, "Attendance Time Set\r\n");
                            Display_Idle_Screen(); /* [추가] 설정 변경 후 화면 갱신 */
                        }
                    } else if (strncmp(cmd_buffer, "SET LATE ", 9) == 0) { /* [추가] SET LATE 명령 */
                        int h, m, s;
                        if (sscanf(cmd_buffer + 9, "%d:%d:%d", &h, &m, &s) == 3) {
                            late_hour = h; late_min = m; late_sec = s;
                            DS3231_SetAlarm2(late_hour, late_min, late_sec);
                            Send_UART_Msg(ACTIVE_USART, "Late Time Set\r\n");
                            Display_Idle_Screen(); /* [추가] 설정 변경 후 화면 갱신 */
                        }
                    } else if (strncmp(cmd_buffer, "SET DEADLINE ", 13) == 0) {
                        int h, m, s;
                        if (sscanf(cmd_buffer + 13, "%d:%d:%d", &h, &m, &s) == 3) {
                            dead_hour = h; dead_min = m; dead_sec = s;
                            DS3231_SetAlarm3(dead_hour, dead_min, dead_sec); /* [수정] Alarm3 사용 */
                            Send_UART_Msg(ACTIVE_USART, "Deadline Time Set\r\n");
                            Display_Idle_Screen(); /* [추가] 설정 변경 후 화면 갱신 */
                        }
                    }
                    
                    cmd_len = 0; // 버퍼 초기화
                }
            } else {
                if (cmd_len < 63) cmd_buffer[cmd_len++] = data;
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
        GPIO_ResetBits(GPIOA, GPIO_Pin_3); Delay(150); 
        GPIO_SetBits(GPIOA, GPIO_Pin_3);   Delay(150); 
    }
}

void Display_Idle_Screen(void) {
    char time_str[20]; // [수정] 변수 선언 맨 위로
    char dbg_str[20];
    char conf_str[40]; // [추가] 설정 시간 표시용 버퍼
    LCD_Clear(WHITE);
    
    DS3231_GetTime(&sTime);

    if(system_active) {
        /* [수정] Attendance vs Late 구분 표시 */
        if (sTime.hours > late_hour || 
           (sTime.hours == late_hour && sTime.minutes > late_min) ||
           (sTime.hours == late_hour && sTime.minutes == late_min && sTime.seconds >= late_sec)) {
            LCD_ShowString(30, 100, (uint8_t*)"LATE", RED, WHITE);
        } else {
            LCD_ShowString(30, 100, (uint8_t*)"ATTENDANCE", GREEN, WHITE);
        }
    } else {
        /* [수정] Idle vs Closed 구분 표시 */
        if (sTime.hours > dead_hour || 
           (sTime.hours == dead_hour && sTime.minutes > dead_min) ||
           (sTime.hours == dead_hour && sTime.minutes == dead_min && sTime.seconds >= dead_sec)) {
            LCD_ShowString(30, 100, (uint8_t*)"CLOSED", RED, WHITE);
        } else {
            LCD_ShowString(30, 100, (uint8_t*)"SYSTEM IDLE", BLUE, WHITE);
        }
    }
    
    sprintf(time_str, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
    LCD_ShowString(30, 130, (uint8_t*)time_str, BLACK, WHITE);

    sprintf(dbg_str, "ACTIVATE: %d", system_active);
    LCD_ShowString(30, 160, (uint8_t*)dbg_str, BLACK, WHITE);

    /* [추가] 설정된 시간 정보 표시 */
    sprintf(conf_str, "ATT: %02d:%02d:%02d", att_hour, att_min, att_sec);
    LCD_ShowString(30, 190, (uint8_t*)conf_str, BLACK, WHITE);

    sprintf(conf_str, "LAT: %02d:%02d:%02d", late_hour, late_min, late_sec);
    LCD_ShowString(30, 210, (uint8_t*)conf_str, BLACK, WHITE);

    sprintf(conf_str, "DED: %02d:%02d:%02d", dead_hour, dead_min, dead_sec);
    LCD_ShowString(30, 230, (uint8_t*)conf_str, BLACK, WHITE);
}

void Delay(__IO uint32_t nTime) {
    TimingDelay = nTime;
    while (TimingDelay != 0);
}

/* --- Configurations --- */
void RCC_Configuration(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE | RCC_APB2Periph_AFIO | RCC_APB2Periph_USART1 | RCC_APB2Periph_SPI1, ENABLE);
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
    GPIO_SetBits(GPIOB, GPIO_Pin_0 | GPIO_Pin_1); /* [수정] 초기 상태 OFF */

    // IRQs
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE); // JTAG 비활성화 (SWD는 유지)
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

    // [수정] RTC Alarm (PA1) EXTI Line 1 설정 추가
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);
    EXTI_InitStructure.EXTI_Line = EXTI_Line1;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = EXTI0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);

    // [수정] RTC Alarm 인터럽트(EXTI1) 활성화
    NVIC_InitStructure.NVIC_IRQChannel = EXTI1_IRQn;
    NVIC_Init(&NVIC_InitStructure);
}

void USART1_IRQHandler(void) {
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET) {
#if UART_SELECT == 1
        /* 링 버퍼에 데이터 저장 */
        uint8_t data = USART_ReceiveData(USART1);
        uint16_t next_head = (rx_head + 1) % RX_BUFFER_SIZE;
        if (next_head != rx_tail) {
            rx_buffer[rx_head] = data;
            rx_head = next_head;
        }
#else
        USART_ReceiveData(USART1); // 사용 안 함
#endif
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void USART2_IRQHandler(void) {
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
#if UART_SELECT == 2
        /* 링 버퍼에 데이터 저장 */
        uint8_t data = USART_ReceiveData(USART2);
        uint16_t next_head = (rx_head + 1) % RX_BUFFER_SIZE;
        if (next_head != rx_tail) {
            rx_buffer[rx_head] = data;
            rx_head = next_head;
        }
#else
        USART_ReceiveData(USART2); // 사용 안 함
#endif
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