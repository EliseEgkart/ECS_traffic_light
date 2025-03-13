#include <Arduino.h>
#include <TaskScheduler.h>
#include <PinChangeInterrupt.h>

// 핀 정의
#define LED_RED       11  // PWM 가능
#define LED_YELLOW    10  // PWM 가능
#define LED_GREEN     9   // PWM 가능

#define BUTTON1       4
#define BUTTON2       3
#define BUTTON3       2
#define POTENTIOMETER A5

// Task 주기 정의
#define RX_DURATION   500
#define TX_DURATION   100
#define TASK_UPDATE_DURATION 10

Scheduler runner;

// LED 패턴 열거형
enum LEDPatternState {
  PATTERN_OFF = 0,
  PATTERN_RED = 1,
  PATTERN_YELLOW = 2,
  PATTERN_GREEN = 3,
  PATTERN_MODE2_TOGGLE = 9
}; // LED 상태를 나타내는 전역 변수
volatile LEDPatternState ledPattern = PATTERN_OFF;

// 기본 신호등 상태 머신
enum TrafficLight_State {
  BLINK_RED,
  BLINK_YELLOW1,
  BLINK_GREEN,
  FLICK_GREEN,
  BLINK_YELLOW2
}; // 신호등 상태를 나타내는 FLAG
volatile TrafficLight_State currentState = BLINK_RED;

// 각 상태의 유지 시간
unsigned int intervalRed    = 2000;
unsigned int intervalYellow = 500;
unsigned int intervalGreen  = 2000;
unsigned int intervalFlick  = 1000 / 6; // 1초 동안 3회 깜빡이기 위해

// 상태 전이를 위한 타이밍 변수
unsigned long stateStartTime = 0;
int flickCount = 0;

// 모드 토글 플래그 (한 번에 하나만 활성화)
volatile bool mode1Active = false;
volatile bool mode2Active = false;
volatile bool mode3Active = false;

// 현재 가변저항에 따른 밝기 (0~255)
volatile int brightness = 255;

// -----------------------------------------------------
// 헬퍼 함수
// -----------------------------------------------------

/**
 * @brief LED에 PWM 값을 한 번에 적용하는 함수.
 *
 * @param r_val Red LED PWM 값.
 * @param y_val Yellow LED PWM 값.
 * @param g_val Green LED PWM 값.
 */
void analogWriteRYG(int r_val, int y_val, int g_val) {
  analogWrite(LED_RED, r_val);
  analogWrite(LED_YELLOW, y_val);
  analogWrite(LED_GREEN, g_val);
}

/**
 * @brief 현재 ledPattern과 brightness에 따라 LED를 출력한다.
 */
void renderLED() {
  switch (ledPattern) {
    case PATTERN_RED:
      analogWriteRYG(brightness, 0, 0);
      break;
    case PATTERN_YELLOW:
      analogWriteRYG(0, brightness, 0);
      break;
    case PATTERN_GREEN:
      analogWriteRYG(0, 0, brightness);
      break;
    default: // PATTERN_OFF 또는 그 외
      analogWriteRYG(0, 0, 0);
      break;
  }
}

/**
 * @brief 모드2 전용 렌더링: 500ms 간격으로 전체 LED를 토글한다.
 */
void renderMode2() {
  static unsigned long prevTime = millis();
  unsigned long now = millis();

  if (now - prevTime >= 500) {
    prevTime = now;
    // 토글: 현재 패턴이 PATTERN_MODE2_TOGGLE이면 OFF, 아니면 PATTERN_MODE2_TOGGLE로 전환
    ledPattern = (ledPattern == PATTERN_MODE2_TOGGLE) ? PATTERN_OFF : PATTERN_MODE2_TOGGLE;
  }

  if (ledPattern == PATTERN_MODE2_TOGGLE) {
    analogWriteRYG(brightness, brightness, brightness);
  } else {
    analogWriteRYG(0, 0, 0);
  }
}

/**
 * @brief 상태 머신 로직 업데이트 함수.
 */
void updateStateMachine() {
  // 모드가 활성화되었으면 상태 머신 동작 중지
  if (mode1Active || mode2Active || mode3Active) return;

  unsigned long now = millis();

  switch (currentState) {
    case BLINK_RED:
      ledPattern = PATTERN_RED;
      if (now - stateStartTime >= intervalRed) {
        stateStartTime = now;
        currentState = BLINK_YELLOW1;
      }
      break;

    case BLINK_YELLOW1:
      ledPattern = PATTERN_YELLOW;
      if (now - stateStartTime >= intervalYellow) {
        stateStartTime = now;
        currentState = BLINK_GREEN;
      }
      break;

    case BLINK_GREEN:
      ledPattern = PATTERN_GREEN;
      if (now - stateStartTime >= intervalGreen) {
        stateStartTime = now;
        currentState = FLICK_GREEN;
        flickCount = 0;
      }
      break;

    case FLICK_GREEN:
      if (now - stateStartTime >= intervalFlick) {
        stateStartTime = now;
        // 깜빡임: GREEN ON/OFF 전환
        ledPattern = (ledPattern == PATTERN_GREEN) ? PATTERN_OFF : PATTERN_GREEN;
        flickCount++;
        if (flickCount >= 6) {
          currentState = BLINK_YELLOW2;
          stateStartTime = now;
        }
      }
      break;

    case BLINK_YELLOW2:
      ledPattern = PATTERN_YELLOW;
      if (now - stateStartTime >= intervalYellow) {
        stateStartTime = now;
        currentState = BLINK_RED;
      }
      break;
  }
}
Task taskStateMachine(TASK_UPDATE_DURATION, TASK_FOREVER, []() {updateStateMachine();});

// -----------------------------------------------------
// 시리얼 모니터 태스크: 밝기와 모드 출력
// -----------------------------------------------------
void serialMonitorTaskCallback() {
  Serial.print("B:");
  Serial.print(brightness);
  Serial.print(" M:");
  if (mode1Active) {
    Serial.print("PCINT1");
  } else if (mode2Active) {
    Serial.print("PCINT2");
  } else if (mode3Active) {
    Serial.print("PCINT3");
  } else {
    Serial.print("Default");
  }

  // LED 상태 결정: ledPattern에 따른 단순 이진 상태
  int rState = 0, yState = 0, gState = 0;
  switch (ledPattern) {
    case PATTERN_RED:
      rState = 1;
      break;
    case PATTERN_YELLOW:
      yState = 1;
      break;
    case PATTERN_GREEN:
      gState = 1;
      break;
    case PATTERN_MODE2_TOGGLE:
      // 모드2의 경우, renderMode2에서 토글 상태를 구현하므로
      // 현재 ledPattern 값이 PATTERN_MODE2_TOGGLE이면 LED가 켜진 상태로 간주.
      rState = 1;
      yState = 1;
      gState = 1;
      break;
    default:
      // PATTERN_OFF 혹은 기타 상태일 경우 모두 0
      break;
  }
  Serial.print(" O:");
  Serial.print(rState);
  Serial.print(",");
  Serial.print(yState);
  Serial.print(",");
  Serial.println(gState);
}

Task taskSerialOutput(TX_DURATION, TASK_FOREVER, &serialMonitorTaskCallback);

// -----------------------------------------------------
// 시리얼 입력 태스크: 유지시간 갱신
// -----------------------------------------------------
void serialInputTaskCallback() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) {
      int firstComma = input.indexOf(',');
      int secondComma = input.indexOf(',', firstComma + 1);
      if (firstComma != -1 && secondComma != -1) {
        unsigned int newRed = input.substring(0, firstComma).toInt();
        unsigned int newYellow = input.substring(firstComma + 1, secondComma).toInt();
        unsigned int newGreen = input.substring(secondComma + 1).toInt();
        // 새로운 값이 유효하면 갱신
        if (newRed > 0 && newYellow > 0 && newGreen > 0) {
          intervalRed = newRed;
          intervalYellow = newYellow;
          intervalGreen = newGreen;
          Serial.print("Intervals updated to: ");
          Serial.print(intervalRed);
          Serial.print(", ");
          Serial.print(intervalYellow);
          Serial.print(", ");
          Serial.println(intervalGreen);
        } else {
          Serial.println("Invalid intervals provided.");
        }
      } else {
        Serial.println("Invalid input format. Use: 2000,500,2000");
      }
    }
  }
}
Task taskSerialInput(RX_DURATION, TASK_FOREVER, &serialInputTaskCallback);

// -----------------------------------------------------
// 버튼 인터럽트 콜백 함수
// -----------------------------------------------------
/**
 * @brief 버튼1 인터럽트: 모드1 토글 (빨간 LED 고정)
 */
void PCINTCallbackButton1() {
  static bool lastState = HIGH;
  bool currentStateBtn = digitalRead(BUTTON1);

  if (lastState == HIGH && currentStateBtn == LOW) {
    if (!mode1Active) {
      mode1Active = true;
      mode2Active = false;
      mode3Active = false;
      // 모드1에서는 상태 머신 대신 빨간 LED를 지속적으로 표시
      ledPattern = PATTERN_RED;
    } else {
      mode1Active = false;
      stateStartTime = millis();
      currentState = BLINK_RED;
    }
  }
  lastState = currentStateBtn;
}

/**
 * @brief 버튼2 인터럽트: 모드2 토글 (전체 LED 500ms 간격 토글)
 */
void PCINTCallbackButton2() {
  static bool lastState = HIGH;
  bool currentStateBtn = digitalRead(BUTTON2);

  if (lastState == HIGH && currentStateBtn == LOW) {
    if (!mode2Active) {
      mode2Active = true;
      mode1Active = false;
      mode3Active = false;
      // 모드2에서는 상태 머신 대신 토글 패턴을 사용
      ledPattern = PATTERN_MODE2_TOGGLE;
    } else {
      mode2Active = false;
      stateStartTime = millis();
      currentState = BLINK_RED;
    }
  }
  lastState = currentStateBtn;
}

/**
 * @brief 버튼3 인터럽트: 모드3 토글 (모든 LED OFF)
 */
void PCINTCallbackButton3() {
  static bool lastState = HIGH;
  bool currentStateBtn = digitalRead(BUTTON3);

  if (lastState == HIGH && currentStateBtn == LOW) {
    if (!mode3Active) {
      mode3Active = true;
      mode1Active = false;
      mode2Active = false;
      ledPattern = PATTERN_OFF;
    } else {
      mode3Active = false;
      stateStartTime = millis();
      currentState = BLINK_RED;
    }
  }
  lastState = currentStateBtn;
}

// -----------------------------------------------------
// Setup 및 Loop 함수
// -----------------------------------------------------
void setup() {
  Serial.begin(9600);

  // 핀 모드 설정
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  pinMode(BUTTON1, INPUT_PULLUP);
  pinMode(BUTTON2, INPUT_PULLUP);
  pinMode(BUTTON3, INPUT_PULLUP);

  // 버튼 인터럽트 설정
  attachPCINT(digitalPinToPCINT(BUTTON1), PCINTCallbackButton1, CHANGE);
  attachPCINT(digitalPinToPCINT(BUTTON2), PCINTCallbackButton2, CHANGE);
  attachPCINT(digitalPinToPCINT(BUTTON3), PCINTCallbackButton3, CHANGE);

  runner.init();
  runner.addTask(taskStateMachine);
  runner.addTask(taskSerialOutput);
  runner.addTask(taskSerialInput);

  taskStateMachine.enable();
  taskSerialOutput.enable();
  taskSerialInput.enable();

  stateStartTime = millis();
}

void loop() {
  // 매 반복마다 가변저항 값을 읽어 brightness 업데이트 (실시간 반영)
  int potVal = analogRead(POTENTIOMETER);
  brightness = map(potVal, 0, 1023, 0, 255);

  // 모드2가 활성화되었으면 모드2 전용 렌더링, 아니면 기본 LED 렌더링
  if (mode2Active) {
    renderMode2();
  } else {
    renderLED();
  }

  runner.execute();
}
