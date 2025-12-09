/* Core/Src/main.c */
#include "main.h"
#include "i2c.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "rc522.h"
#include "ds3231.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <string.h>

// --- 전역 변수 ---
volatile uint8_t rfid_irq_flag = 0;  // RFID 인터럽트 플래그
volatile uint8_t rtc_alarm_flag = 0; // RTC 알람 플래그
volatile uint8_t uart_rx_data;       // UART 수신 데이터
uint8_t system_active = 0;           // 0: 출석 마감/대기, 1: 출석 시작(태그 인식 가능)

RTC_TimeTypeDef sTime;
char str_buff[64];

// --- 학생 DB (UID 4바이트 + 이름) ---
struct {
    uint8_t uid[4];
    char name[10];
} db[] = {
    {{0x12, 0x34, 0x56, 0x78}, "LeeNY"},      // 예시 데이터 수정 필요
    {{0xAA, 0xBB, 0xCC, 0xDD}, "SeungWoo"},
    {{0xFF, 0xEE, 0xDD, 0xCC}, "Andrea"}
};

// 함수 원형 선언
void SystemClock_Config(void);
void Beep(int count);
void Check_Attendance_Status(void);
void Display_Idle_Screen(void);

// [기능 1] 부저 동작 함수 (count 횟수만큼 울림)
void Beep(int count) {
    for (int i = 0; i < count; i++) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET); // 부저 ON
        HAL_Delay(100);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET); // 부저 OFF
        HAL_Delay(100);
    }
}

// [기능 2] UART 수신 인터럽트 (명령어 처리)
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        switch (uart_rx_data) {
        case 'R':   // RESET
            // 출석 로그 초기화, 상태 초기화 등 (지금은 부저로만 피드백)
            Beep(2);
            break;

        case 'E':   // EXPORT
            // 여기서는 "지금부터 태그 인식 시마다 바로 보내고 있으니,
            // 추가 로그는 나중에 구현" 정도로 플레이스홀더
            // ex) "EXPORT NOT SUPPORTED\r\n" 전송
            {
                char msg[] = "EXPORT NOT IMPLEMENTED\r\n";
                HAL_UART_Transmit(&huart1, (uint8_t*)msg, strlen(msg), 100);
            }
            break;

        case 'S':   // SET DEADLINE (간단 버전: system_active 토글 정도)
            // 진짜로 시간 변경하려면 추가 프로토콜 설계 필요
            system_active = !system_active;
            break;

        default:
            break;
        }

        // 다음 수신 대기
        HAL_UART_Receive_IT(&huart1, (uint8_t*)&uart_rx_data, 1);
    }
}


// [기능 3] GPIO 외부 인터럽트 (RFID & RTC) [cite: 91]
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) { 
        rfid_irq_flag = 1; // RC522 IRQ (PA0)
    } 
    else if (GPIO_Pin == GPIO_PIN_1) { 
        rtc_alarm_flag = 1; // RTC INT/SQW (PA1 - 회로 연결 확인 필요)
    }
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();

  // 1. 모듈 초기화
  DS3231_Init(&hi2c1);
  // RC522_Init();
  MFRC522_Init();
  ssd1306_Init();

  // 2. UART 수신 대기 시작
  HAL_UART_Receive_IT(&huart1, (uint8_t*)&uart_rx_data, 1);

  // 3. RTC 알람 설정 (매일 09시 00분 00초에 울리도록 설정) [cite: 83]
  // 주의: 테스트할 때는 현재 시간보다 1~2분 뒤로 설정하세요.
  DS3231_SetAlarm1(9, 0, 0); 
  DS3231_ClearAlarmFlags();

  // 초기 화면: 대기 상태
  Display_Idle_Screen();

  while (1)
  {
    // ---------------------------------------------------------
    // A. RTC 알람 처리 (수업 시작/종료 제어) [cite: 83, 89]
    // ---------------------------------------------------------
    if (rtc_alarm_flag) {
        rtc_alarm_flag = 0;
        DS3231_ClearAlarmFlags(); // 알람 플래그 해제

        // 현재 시간 확인
        DS3231_GetTime(&sTime);

        // [시나리오] 09:00 되면 출석 시작
        if (sTime.hours == 9 && sTime.minutes == 0) {
            system_active = 1; // 출석 모드 활성화
            /*
            ssd1306_Clear();
            ssd1306_GotoXY(10, 20);
            ssd1306_Puts("ATTENDANCE", &Font_7x10, 1);
            ssd1306_GotoXY(25, 35);
            ssd1306_Puts("OPEN!", &Font_11x18, 1);
            ssd1306_UpdateScreen();
            Beep(3); // 시작 알림 3번*/

            ssd1306_Fill(Black);
            ssd1306_SetCursor(10, 20);
            ssd1306_WriteString("ATTENDANCE", Font_7x10, White);
            ssd1306_SetCursor(25, 35);
            ssd1306_WriteString("OPEN!",      Font_11x18, White);
            ssd1306_UpdateScreen();
        }
        // [시나리오] 10:00 되면 마감 (예시)
        else if (sTime.hours == 10 && sTime.minutes == 0) {
            system_active = 0; // 출석 모드 비활성화
            Display_Idle_Screen();
            Beep(3); // 종료 알림
        }
    }

    // ---------------------------------------------------------
    // B. RFID 태그 인식 처리 (활성화 상태일 때만) [cite: 84]
    // ---------------------------------------------------------
    if (system_active && rfid_irq_flag) {
        rfid_irq_flag = 0; 
        uint8_t uid[5];

        // 카드 읽기 시도
        if (RC522_Check(uid) == MI_OK) {
            
            // 1. DB 검색 (등록 여부 확인)
            int user_idx = -1;
            int db_size = sizeof(db) / sizeof(db[0]);
            
            for(int i=0; i < db_size; i++) {
                // UID 4바이트 비교
                if(memcmp(db[i].uid, uid, 4) == 0) {
                    user_idx = i;
                    break;
                }
            }

            // 2. 현재 시간 가져오기 (지각 판정용)
            DS3231_GetTime(&sTime);
            ssd1306_Fill(Black);

            if (user_idx != -1) {
                // --- [등록된 학생] ---
                char status[10];
                
                // 지각 판정 로직 (예: 9시 10분 이후 지각)
                if (sTime.hours > 9 || (sTime.hours == 9 && sTime.minutes > 10)) {
                    strcpy(status, "LATE");
                    // 빨강 LED + 부저 2회 
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
                    Beep(2); 
                } else {
                    strcpy(status, "OK");
                    // 초록 LED + 부저 1회 
                    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
                    Beep(1);
                }

                // ----- OLED 출력 -----
                sprintf(str_buff, "Name: %s", db[user_idx].name);
                ssd1306_SetCursor(0, 10);
                ssd1306_WriteString(str_buff, Font_7x10, White);

                sprintf(str_buff, "%02d:%02d [%s]", sTime.hours, sTime.minutes, status);
                ssd1306_SetCursor(0, 30);
                ssd1306_WriteString(str_buff, Font_11x18, White);
                
                // 블루투스 전송
                uint8_t uart_buff[64];
                sprintf((char*)uart_buff, "%s,%02d:%02d,%s\r\n",
                        db[user_idx].name, sTime.hours, sTime.minutes, status);
                HAL_UART_Transmit(&huart1, uart_buff, strlen((char*)uart_buff), 100);

            } else {
                // --- [미등록 학생] ---
                ssd1306_SetCursor(10, 20);
                ssd1306_WriteString("UNKNOWN TAG", Font_11x18, White);
                
                // uid 확인용
                 sprintf(str_buff,
                        "%02X %02X %02X %02X",
                        uid[0], uid[1], uid[2], uid[3]);
                ssd1306_SetCursor(0, 30);
                ssd1306_WriteString(str_buff, Font_11x18, White);

                // 블루투스
                uint8_t uart_buff[64];
                sprintf((char*)uart_buff, "UNKNOWN,%02X%02X%02X%02X\r\n",
                        uid[0], uid[1], uid[2], uid[3]);
                HAL_UART_Transmit(&huart1, uart_buff, strlen((char*)uart_buff), 100);

                // 노랑 LED (Red+Green) + 부저 1회 
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_SET); 
                Beep(1);
            }

            // 실제로 그린 걸 화면에 반영
            ssd1306_UpdateScreen();

            // LED 표시 및 결과 확인을 위해 2초 대기
            HAL_Delay(2000);

            // LED 끄기
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_RESET);

            // 다시 '출석중' 화면으로 복귀
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

// 대기 화면 함수 (현재 시간 표시) [cite: 82]
void Display_Idle_Screen(void) {
    ssd1306_Fill(Black);
    ssd1306_SetCursor(20, 10);
    ssd1306_WriteString("SYSTEM IDLE", Font_7x10, White);
    
    DS3231_GetTime(&sTime);
    sprintf(str_buff, "%02d:%02d:%02d", sTime.hours, sTime.minutes, sTime.seconds);
    ssd1306_SetCursor(15, 30);
    ssd1306_WriteString(str_buff,       Font_11x18, White);
    
    ssd1306_UpdateScreen();
}