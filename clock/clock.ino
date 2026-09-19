#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DS1302.h>

#define JOY_X_PIN   A0
#define JOY_Y_PIN   A1
#define JOY_SW_PIN  7
#define MODE_BTN    4
#define RESET_BTN   5

// DS1302 핀 (CE, IO, SCLK) ★
#define RTC_CE     11
#define RTC_IO     10
#define RTC_CLK     9

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DS1302 rtc(RTC_CE, RTC_IO, RTC_CLK);

// ==================== 모드 ====================
int mode = 0;   // 0=시계, 1=스톱워치, 2=타이머
const int MODE_COUNT = 3;
const char* MODE_NAMES[3] = {"CLOCK", "STOPWATCH", "TIMER"};

// ==================== 조이스틱 ====================
int joyCX = 512, joyCY = 512;
unsigned long lastAdjust = 0;
const int JOY_DEAD = 200;

// ==================== 시계 ====================
int clockField = 0;   // 0=시, 1=분
bool clockNeedWrite = false;
unsigned long lastClockWrite = 0;

// ==================== 스톱워치 ====================
unsigned long swStart = 0;
unsigned long swAccum = 0;
bool swRunning = false;

// ==================== 타이머 ====================
long timerSetSec = 60;
long timerRemain = 60;
bool timerRunning = false;
unsigned long timerLastTick = 0;
int timerField = 0;          // 0=시, 1=분, 2=초
bool timerAlarm = false;

// ==================== 버튼 ====================
int lastModeBtn = HIGH;
unsigned long modeBtnDebounce = 0;
int lastSwBtn = HIGH;
unsigned long swBtnDebounce = 0;
int lastResetBtn = HIGH;
unsigned long resetBtnDebounce = 0;

// ==================== 프로토타입 ====================
void calibrateJoystick();
void initRTC();
void handleModeButton();
void handleSwButton();
void handleResetButton();
void handleJoystickAdjust();
void drawClock();
void drawStopwatch();
void drawTimer();
void drawHeader(const char* name);
void formatMS(unsigned long totalMs, char* buf);
void resetCurrentMode();
void adjustClockField(int delta);

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  pinMode(MODE_BTN, INPUT_PULLUP);
  pinMode(RESET_BTN, INPUT_PULLUP);

  // ---- DS1302 초기화 ----
  initRTC();

  // ---- OLED ----
  Wire.begin();
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 fail"));
    for (;;);
  }
  display.ssd1306_command(0x81);
  display.ssd1306_command(255);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Calibrating..."));
  display.println(F("Don't touch stick"));
  display.display();

  calibrateJoystick();
  Wire.setClock(400000);
}

// ==================== DS1302 초기화 ====================
void initRTC() {
  // 쓰기 보호 해제, halt 해제 (오실레이터 동작)
  rtc.halt(false);
  rtc.writeProtect(false);

  // 처음 한 번만 시간 설정하고 싶으면 아래 주석 해제해서 업로드 후 다시 주석
  // rtc.setDOW(SATURDAY);
  // rtc.setTime(15, 37, 50);      // 12:00:00
  // rtc.setDate(19, 9, 2026);  // 2026년 9월 19일

  Serial.print(F("RTC Time: "));
  Serial.println(rtc.getTimeStr());
  Serial.print(F("RTC Date: "));
  Serial.println(rtc.getDateStr());
}

void calibrateJoystick() {
  delay(300);
  long sx = 0, sy = 0;
  for (int i = 0; i < 30; i++) {
    sx += analogRead(JOY_X_PIN);
    sy += analogRead(JOY_Y_PIN);
    delay(5);
  }
  joyCX = sx / 30;
  joyCY = sy / 30;
}

// ==================== 시계 조정 ====================
void adjustClockField(int delta) {
  Time t = rtc.getTime();

  int h = t.hour;
  int m = t.min;
  int s = t.sec;
  int dow = t.dow;
  int day = t.date;
  int mon = t.mon;
  int year = t.year;

  if (clockField == 0) {
    // 시 조정
    h += delta;
    if (h >= 24) h -= 24;
    if (h < 0) h += 24;
  } else {
    // 분 조정
    m += delta;
    if (m >= 60) { m -= 60; h++; if (h >= 24) h -= 24; }
    if (m < 0)   { m += 60; h--; if (h < 0) h += 24; }
  }

  rtc.setTime(h, m, s);
  // 날짜도 그대로 유지
  rtc.setDate(day, mon, year);
  rtc.setDOW(dow);
}

// ==================== 버튼 ====================
void handleModeButton() {
  int reading = digitalRead(MODE_BTN);
  unsigned long now = millis();
  if (reading != lastModeBtn) modeBtnDebounce = now;

  if (now - modeBtnDebounce > 50) {
    static int stable = HIGH;
    if (reading != stable) {
      stable = reading;
      if (stable == LOW) {
        mode = (mode + 1) % MODE_COUNT;
        if (mode == 2) {
          timerRemain = timerSetSec;
          timerRunning = false;
          timerAlarm = false;
        }
      }
    }
  }
  lastModeBtn = reading;
}

void handleSwButton() {
  int reading = digitalRead(JOY_SW_PIN);
  unsigned long now = millis();
  if (reading != lastSwBtn) swBtnDebounce = now;

  if (now - swBtnDebounce > 50) {
    static int stable = HIGH;
    if (reading != stable) {
      stable = reading;
      if (stable == LOW) {
        if (mode == 1) {
          if (swRunning) {
            swAccum += now - swStart;
            swRunning = false;
          } else {
            swStart = now;
            swRunning = true;
          }
        }
        else if (mode == 2 && !timerAlarm) {
          if (timerRunning) {
            unsigned long elapsed = now - timerLastTick;
            long consumed = elapsed / 1000;
            timerRemain -= consumed;
            if (timerRemain < 0) timerRemain = 0;
            timerRunning = false;
          } else {
            if (timerRemain > 0) {
              timerRunning = true;
              timerLastTick = now;
            }
          }
        }
      }
    }
  }
  lastSwBtn = reading;
}

void handleResetButton() {
  int reading = digitalRead(RESET_BTN);
  unsigned long now = millis();
  if (reading != lastResetBtn) resetBtnDebounce = now;

  if (now - resetBtnDebounce > 50) {
    static int stable = HIGH;
    if (reading != stable) {
      stable = reading;
      if (stable == LOW) {
        resetCurrentMode();
      }
    }
  }
  lastResetBtn = reading;
}

void resetCurrentMode() {
  if (mode == 1) {
    swAccum = 0;
    swRunning = false;
    swStart = millis();
  }
  else if (mode == 2) {
    timerRemain = timerSetSec;
    timerRunning = false;
    timerAlarm = false;
  }
}

// ==================== 조이스틱 조정 ====================
void handleJoystickAdjust() {
  unsigned long now = millis();
  if (now - lastAdjust < 150) return;

  int jx = analogRead(JOY_X_PIN) - joyCX;
  int jy = analogRead(JOY_Y_PIN) - joyCY;

  bool changed = false;

  if (mode == 0) {
    if (jx < -JOY_DEAD) { clockField = 0; changed = true; }
    if (jx >  JOY_DEAD) { clockField = 1; changed = true; }
    if (jy < -JOY_DEAD) { adjustClockField(1); changed = true; }
    if (jy >  JOY_DEAD) { adjustClockField(-1); changed = true; }
  }
  else if (mode == 2 && !timerRunning && !timerAlarm) {
    long oldSet = timerSetSec;

    if (jx < -JOY_DEAD) { timerField = (timerField + 2) % 3; changed = true; }
    if (jx >  JOY_DEAD) { timerField = (timerField + 1) % 3; changed = true; }

    if (jy < -JOY_DEAD) {
      if      (timerField == 0) timerSetSec += 3600;
      else if (timerField == 1) timerSetSec += 60;
      else                      timerSetSec += 1;
      changed = true;
    }
    if (jy >  JOY_DEAD) {
      if      (timerField == 0) timerSetSec -= 3600;
      else if (timerField == 1) timerSetSec -= 60;
      else                      timerSetSec -= 1;
      changed = true;
    }
    if (timerSetSec < 0) timerSetSec = 0;
    if (timerSetSec > 359999) timerSetSec = 359999;

    if (timerSetSec != oldSet) {
      timerRemain = timerSetSec;
    }
  }

  if (changed) lastAdjust = now;
}

// ==================== 포맷 ====================
void formatMS(unsigned long totalMs, char* buf) {
  unsigned long cs = (totalMs / 10) % 100;
  unsigned long totalSec = totalMs / 1000;
  int m = (totalSec / 60) % 100;
  int s = totalSec % 60;
  sprintf(buf, "%02d:%02d.%02d", m, s, (int)cs);
}

// ==================== 화면 ====================
void drawHeader(const char* name) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("[ "));
  display.print(name);
  display.print(F(" ]"));
  for (int i = 0; i < MODE_COUNT; i++) {
    int x = 100 + i * 8;
    if (i == mode) display.fillCircle(x, 4, 2, SSD1306_WHITE);
    else display.drawCircle(x, 4, 2, SSD1306_WHITE);
  }
}

void drawClock() {
  drawHeader("CLOCK");

  Time t = rtc.getTime();
  int h = t.hour;
  int m = t.min;
  int s = t.sec;

  char buf[12];
  sprintf(buf, "%02d:%02d:%02d", h, m, s);

  display.setTextSize(2);
  display.setCursor(16, 22);
  display.print(buf);

  display.setTextSize(1);
  display.setCursor(0, 56);
  if (clockField == 0) {
    display.print(F("Edit: HOUR"));
    display.fillRect(18, 52, 22, 1, SSD1306_WHITE);
  } else {
    display.print(F("Edit: MIN"));
    display.fillRect(42, 52, 20, 1, SSD1306_WHITE);
  }
  display.setCursor(96, 56);
  display.print(F("D4>"));
}

void drawStopwatch() {
  drawHeader("STOPWATCH");

  unsigned long total = swAccum;
  if (swRunning) total += millis() - swStart;

  char buf[16];
  formatMS(total, buf);

  display.setTextSize(2);
  display.setCursor(20, 22);
  display.print(buf);

  display.setTextSize(1);
  display.setCursor(0, 56);
  if (swRunning) display.print(F("RUNNING"));
  else display.print(F("PAUSED"));
  display.setCursor(80, 56);
  display.print(F("D5:RST"));

  if (swRunning && (millis() / 500) % 2) {
    display.fillCircle(70, 59, 2, SSD1306_WHITE);
  }
}

void drawTimer() {
  drawHeader("TIMER");

  long remain = timerRemain;
  if (timerRunning) {
    unsigned long elapsed = millis() - timerLastTick;
    remain = timerRemain - (elapsed / 1000);
    if (remain < 0) remain = 0;
  }

  int h = remain / 3600;
  int m = (remain / 60) % 60;
  int s = remain % 60;

  char buf[16];
  sprintf(buf, "%02d:%02d:%02d", h, m, s);

  display.setTextSize(2);
  display.setCursor(16, 22);
  display.print(buf);

  display.setTextSize(1);
  display.setCursor(0, 56);

  if (timerAlarm) {
    display.setTextSize(3);
    display.setCursor(16, 20);
    display.print(F("TIME!"));
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print(F("D5:RST"));
  }
  else if (timerRunning) {
    display.print(F("RUNNING"));
    display.setCursor(74, 56);
    display.print(F("SW:PAUSE"));
  }
  else {
    if (timerRemain == timerSetSec) {
      if      (timerField == 0) display.print(F("Set: HOUR"));
      else if (timerField == 1) display.print(F("Set: MIN"));
      else                      display.print(F("Set: SEC"));

      int underlineX = 0, underlineW = 0;
      if (timerField == 0) { underlineX = 17; underlineW = 22; }
      else if (timerField == 1) { underlineX = 41; underlineW = 22; }
      else { underlineX = 65; underlineW = 22; }
      display.fillRect(underlineX, 52, underlineW, 1, SSD1306_WHITE);

      display.setCursor(88, 56);
      display.print(F("SW:GO"));
    } else {
      display.print(F("PAUSED"));
      display.setCursor(74, 56);
      display.print(F("SW:RESUME"));
    }
  }
}

// ==================== loop ====================
void loop() {
  unsigned long now = millis();

  handleModeButton();
  handleSwButton();
  handleResetButton();
  handleJoystickAdjust();

  if (mode == 2 && timerRunning && !timerAlarm) {
    unsigned long elapsed = now - timerLastTick;
    if (elapsed >= 1000) {
      unsigned long ticks = elapsed / 1000;
      timerLastTick += ticks * 1000;
      if (timerRemain >= (long)ticks) timerRemain -= ticks;
      else timerRemain = 0;

      if (timerRemain == 0) {
        timerRunning = false;
        timerAlarm = true;
      }
    }
  }

  display.clearDisplay();
  if (mode == 0) drawClock();
  else if (mode == 1) drawStopwatch();
  else drawTimer();
  display.display();
}