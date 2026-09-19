#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 초음파 센서 핀
const int TRIG_PIN = 5;
const int ECHO_PIN = 6;

// 버튼 핀
const int BUTTON_PIN = 4;

// 거리 범위 (cm)
const float MIN_DIST = 4.0;
const float MAX_DIST = 500.0;

// 측정 주기
const unsigned long MEASURE_INTERVAL = 100;
unsigned long lastMeasure = 0;

// 디바운싱용
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;
int lastButtonState = HIGH;

// 화면 모드
int screenMode = 0;              // 0 = 거리측정, 1 = 테스트 화면
const int SCREEN_COUNT = 2;

// 부드러운 애니메이션
float smoothedDist = 0;
bool firstRead = true;

void setup() {
  Serial.begin(9600);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Initializing..."));
  display.display();
  delay(500);
}

void loop() {
  handleButton();

  // 거리 측정은 항상 해두면 화면 전환 시 바로 반영돼
  if (millis() - lastMeasure >= MEASURE_INTERVAL) {
    lastMeasure = millis();

    float dist = measureDistance();
    if (dist >= MIN_DIST && dist <= MAX_DIST) {
      if (firstRead) {
        smoothedDist = dist;
        firstRead = false;
      } else {
        smoothedDist = smoothedDist * 0.8 + dist * 0.2;
      }
    }
  }

  // 현재 모드에 맞게 그리기
  switch (screenMode) {
    case 0:
      drawDistanceScreen(smoothedDist);
      break;
    case 1:
      drawTestScreen();
      break;
  }
}

// ============================================
// 버튼 처리 (엣지 감지 + 디바운싱)
// ============================================
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    static int stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      // 눌린 순간 (HIGH → LOW)
      if (stableState == LOW) {
        screenMode = (screenMode + 1) % SCREEN_COUNT;
        Serial.print(F("Screen mode: "));
        Serial.println(screenMode);
      }
    }
  }

  lastButtonState = reading;
}

// ============================================
// 화면 1: 거리 측정 + 표정
// ============================================
void drawDistanceScreen(float d) {
  display.clearDisplay();

  // 상단: 거리 텍스트
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Dist: "));
  if (d > 0) {
    display.print(d, 1);
    display.print(F(" cm"));
  } else {
    display.print(F("---"));
  }

  // 중단: 얼굴
  float mood = distanceToMood(d);
  drawFace(mood);

  // 하단: 감정 바
  int barX = 4, barY = 58, barW = 120, barH = 6;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  int fillW = (int)((barW - 2) * mood);
  if (fillW > 0) {
    display.fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);
  }

  display.display();
}

// ============================================
// 화면 2: 테스트 화면 (여기 원하는 걸로 바꿔)
// ============================================
void drawTestScreen() {
  display.clearDisplay();

  // 헤더
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("== TEST SCREEN =="));

  // 가운데 큰 텍스트
  display.setTextSize(2);
  display.setCursor(10, 20);
  display.println(F("HELLO"));

  // 움직이는 점 애니메이션 (프레임 대신 millis 사용)
  int x = (millis() / 20) % 128;
  display.fillCircle(x, 55, 3, SSD1306_WHITE);

  // 하단 힌트
  display.setTextSize(1);
  display.setCursor(0, 57);
  display.print(F("Press btn to go back"));

  display.display();
}

// ============================================
// 나머지 헬퍼 함수 (이전과 동일)
// ============================================
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;

  return duration * 0.0343 / 2.0;
}

float distanceToMood(float d) {
  if (d <= MIN_DIST) return 1.0;
  if (d >= MAX_DIST) return 0.0;
  return 1.0 - (d - MIN_DIST) / (MAX_DIST - MIN_DIST);
}

void drawFace(float mood) {
  int cx = 64;
  int cy = 40;
  int r  = 22;

  display.drawCircle(cx, cy, r, SSD1306_WHITE);

  // 눈
  int eyeY = cy - 6;
  int browInnerY = eyeY - 6 + (int)((1.0 - mood) * -3);
  int browOuterY = eyeY - 6 + (int)(mood * 2);

  display.drawLine(cx - 14, browOuterY, cx - 5, browInnerY, SSD1306_WHITE);
  display.drawLine(cx + 5, browInnerY, cx + 14, browOuterY, SSD1306_WHITE);

  if (mood > 0.55) {
    int lift = (int)((mood - 0.55) * 12);
    display.drawLine(cx - 11, eyeY, cx - 7, eyeY - lift, SSD1306_WHITE);
    display.drawLine(cx - 7,  eyeY - lift, cx - 3, eyeY, SSD1306_WHITE);
    display.drawLine(cx + 3,  eyeY, cx + 7, eyeY - lift, SSD1306_WHITE);
    display.drawLine(cx + 7,  eyeY - lift, cx + 11, eyeY, SSD1306_WHITE);
  } else {
    display.fillCircle(cx - 7, eyeY, 2, SSD1306_WHITE);
    display.fillCircle(cx + 7, eyeY, 2, SSD1306_WHITE);
  }

  // 입
  int mouthW = 20;
  int mouthY = cy + 9;
  int curve  = (int)((0.5 - mood) * 14);  // mood=1 → -7 (웃음)

  for (int x = -mouthW / 2; x <= mouthW / 2; x++) {
    float nx = (float)x / (mouthW / 2.0);
    int dy = (int)(curve * (1.0 - nx * nx));

    int px = cx + x;
    int py = mouthY - dy;

    display.drawPixel(px, py, SSD1306_WHITE);
    display.drawPixel(px, py + 1, SSD1306_WHITE);
  }
}