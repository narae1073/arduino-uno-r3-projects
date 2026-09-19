#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"

// ==================== 핀 ====================
#define TRIG_PIN       5
#define ECHO_PIN       6
#define BUTTON_PIN     4
#define INTERRUPT_PIN  2
#define LED_PIN        13

// ==================== OLED ====================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== MPU6050 ====================
MPU6050 mpu;
bool dmpReady = false;
uint8_t mpuIntStatus;
uint8_t devStatus;
uint16_t packetSize;
uint16_t fifoCount;
uint8_t fifoBuffer[64];
Quaternion q;
VectorFloat gravity;
float ypr[3];
float yprOffset[3] = {0, 0, 0};
volatile bool mpuInterrupt = false;

void dmpDataReady() { mpuInterrupt = true; }

// ==================== 초음파 ====================
const float MIN_DIST = 4.0;
const float MAX_DIST = 500.0;
float smoothedDist = 0;
bool firstDistRead = true;
unsigned long lastMeasure = 0;
const unsigned long MEASURE_INTERVAL = 100;

// ==================== 화면 ====================
int screenMode = 0;              // 0=거리, 1=게임
const int SCREEN_COUNT = 2;

// ==================== 버튼 ====================
int lastButtonReading = HIGH;
unsigned long lastDebounce = 0;
unsigned long buttonPressStart = 0;
bool buttonHeld = false;
const unsigned long DEBOUNCE_MS = 40;
const unsigned long LONG_PRESS_MS = 600;

// ==================== 게임 ====================
struct Obstacle {
  float x;
  float y;
  float vy;
  int r;
  bool active;
};
const int MAX_OBS = 4;
Obstacle obstacles[MAX_OBS];

float planeX, planeY;
unsigned long gameStartMs = 0;
int gameScore = 0;
bool gameOver = false;
unsigned long gameOverMs = 0;
unsigned long lastFrame = 0;
const unsigned long FRAME_INTERVAL = 33;   // ~30fps

// 별 배경
struct Star { uint8_t x, y; };
const int STAR_COUNT = 20;
Star stars[STAR_COUNT];

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(INTERRUPT_PIN, INPUT);

  // ---- 1) I2C 시작 + 타임아웃 ----
  Wire.begin();
  // Wire.setWireTimeout(3000, true);

  // ---- 2) OLED를 먼저 100kHz에서 초기화 ----
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 fail"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("OLED OK"));
  display.println(F("Init MPU..."));
  display.display();
  Serial.println(F("OLED OK"));

  // ---- 3) 이제 I2C 클럭 올리기 ----
  Wire.setClock(400000);

  // ---- 4) MPU6050 초기화 ----
  mpu.initialize();
  mpu.reset();
  delay(100);
  mpu.initialize();

  devStatus = mpu.dmpInitialize();

  mpu.setXGyroOffset(220);
  mpu.setYGyroOffset(76);
  mpu.setZGyroOffset(-85);
  mpu.setZAccelOffset(1788);

  if (devStatus == 0) {
    // Calibrate는 리셋 위험이 있어서 생략 (오프셋으로 충분)
    mpu.setDMPEnabled(true);
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();
    dmpReady = true;
    packetSize = mpu.dmpGetFIFOPacketSize();
    Serial.println(F("DMP ready"));
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print(F("DMP fail "));
    display.println(devStatus);
    display.display();
    Serial.print(F("DMP fail "));
    Serial.println(devStatus);
    while (1);
  }

  // ---- 5) 별 초기화 ----
  randomSeed(analogRead(A0));
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x = random(0, 128);
    stars[i].y = random(0, 64);
  }

  initGame();
  delay(300);
}

// ==================== loop ====================
void loop() {
  if (!dmpReady) return;

  // ---- MPU 인터럽트 처리 ----
  if (mpuInterrupt) {
    mpuInterrupt = false;
    mpuIntStatus = mpu.getIntStatus();
    fifoCount = mpu.getFIFOCount();

    if ((mpuIntStatus & 0x10) || fifoCount == 1024) {
      mpu.resetFIFO();
    } else if (mpuIntStatus & 0x02) {
      while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
      mpu.getFIFOBytes(fifoBuffer, packetSize);
      fifoCount -= packetSize;
      mpu.dmpGetQuaternion(&q, fifoBuffer);
      mpu.dmpGetGravity(&gravity, &q);
      mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    }
  }

  // ---- FIFO 오버플로 방지 ----
  if (mpu.getFIFOCount() > 900) mpu.resetFIFO();

  // ---- 버튼 처리 ----
  handleButton();

  // ---- 거리 측정 ----
  if (millis() - lastMeasure >= MEASURE_INTERVAL) {
    lastMeasure = millis();
    float d = measureDistance();
    if (d >= MIN_DIST && d <= MAX_DIST) {
      if (firstDistRead) {
        smoothedDist = d;
        firstDistRead = false;
      } else {
        smoothedDist = smoothedDist * 0.8 + d * 0.2;
      }
    }
  }

  // ---- 화면 렌더 ----
  if (screenMode == 0) {
    drawDistanceScreen(smoothedDist);
  } else {
    updateAndDrawGame();
  }
}

// ==================== 버튼 ====================
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) lastDebounce = millis();

  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    static int stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        buttonPressStart = millis();
        buttonHeld = true;
      } else {
        // 뗌 → 짧게 누른 경우
        if (buttonHeld) {
          unsigned long held = millis() - buttonPressStart;
          if (held < LONG_PRESS_MS) {
            screenMode = (screenMode + 1) % SCREEN_COUNT;
            if (screenMode == 1) initGame();
            Serial.print(F("Screen ")); Serial.println(screenMode);
          }
          buttonHeld = false;
        }
      }
    }
  }
  lastButtonReading = reading;

  // 길게 누름 → 게임 화면에서 영점 재설정
  if (buttonHeld && (millis() - buttonPressStart >= LONG_PRESS_MS)) {
    if (screenMode == 1) {
      yprOffset[0] = ypr[0] * 180 / M_PI;
      yprOffset[1] = ypr[1] * 180 / M_PI;
      yprOffset[2] = ypr[2] * 180 / M_PI;
      planeX = 64; planeY = 32;
      Serial.println(F("Zero set"));
    }
    buttonHeld = false;
  }
}

// ==================== 초음파 ====================
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

// ==================== 거리 화면 ====================
float distanceToMood(float d) {
  if (d <= MIN_DIST) return 1.0;
  if (d >= MAX_DIST) return 0.0;
  return 1.0 - (d - MIN_DIST) / (MAX_DIST - MIN_DIST);
}

void drawDistanceScreen(float d) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("Dist: "));
  if (d > 0) { display.print(d, 1); display.print(F(" cm")); }
  else display.print(F("---"));

  float mood = distanceToMood(d);
  drawFace(mood);

  int barX = 4, barY = 58, barW = 120, barH = 6;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  int fillW = (int)((barW - 2) * mood);
  if (fillW > 0) display.fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);

  display.display();
}

void drawFace(float mood) {
  int cx = 64, cy = 40, r = 22;
  display.drawCircle(cx, cy, r, SSD1306_WHITE);

  int eyeY = cy - 6;
  int browInnerY = eyeY - 6 + (int)((1.0 - mood) * -3);
  int browOuterY = eyeY - 6 + (int)(mood * 2);
  display.drawLine(cx - 14, browOuterY, cx - 5, browInnerY, SSD1306_WHITE);
  display.drawLine(cx + 5, browInnerY, cx + 14, browOuterY, SSD1306_WHITE);

  if (mood > 0.55) {
    int lift = (int)((mood - 0.55) * 12);
    display.drawLine(cx - 11, eyeY, cx - 7, eyeY - lift, SSD1306_WHITE);
    display.drawLine(cx - 7, eyeY - lift, cx - 3, eyeY, SSD1306_WHITE);
    display.drawLine(cx + 3, eyeY, cx + 7, eyeY - lift, SSD1306_WHITE);
    display.drawLine(cx + 7, eyeY - lift, cx + 11, eyeY, SSD1306_WHITE);
  } else {
    display.fillCircle(cx - 7, eyeY, 2, SSD1306_WHITE);
    display.fillCircle(cx + 7, eyeY, 2, SSD1306_WHITE);
  }

  int mouthW = 20, mouthY = cy + 9;
  int curve = (int)((0.5 - mood) * 14);
  for (int x = -mouthW / 2; x <= mouthW / 2; x++) {
    float nx = (float)x / (mouthW / 2.0);
    int dy = (int)(curve * (1.0 - nx * nx));
    int px = cx + x, py = mouthY - dy;
    display.drawPixel(px, py, SSD1306_WHITE);
    display.drawPixel(px, py + 1, SSD1306_WHITE);
  }
}

// ==================== 게임 ====================
void initGame() {
  planeX = 64;
  planeY = 32;
  gameStartMs = millis();
  gameScore = 0;
  gameOver = false;
  for (int i = 0; i < MAX_OBS; i++) obstacles[i].active = false;
  lastFrame = millis();
}

void spawnObstacle() {
  for (int i = 0; i < MAX_OBS; i++) {
    if (!obstacles[i].active) {
      obstacles[i].active = true;
      obstacles[i].x = random(8, 120);
      obstacles[i].y = -5;
      obstacles[i].r = random(3, 6);
      obstacles[i].vy = random(80, 150) / 100.0 * 1.5;
      return;
    }
  }
}

void updateAndDrawGame() {
  unsigned long now = millis();

  // 게임오버 화면
  if (gameOver) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(14, 10);
    display.println(F("GAME OVER"));
    display.setTextSize(1);
    display.setCursor(34, 35);
    display.print(F("Score: "));
    display.print(gameScore);
    display.setCursor(16, 50);
    display.println(F("Resetting..."));
    display.display();
    if (now - gameOverMs > 2000) initGame();
    return;
  }

  // 프레임 제한
  if (now - lastFrame < FRAME_INTERVAL) return;
  float dt = (now - lastFrame) / 1000.0;
  lastFrame = now;

  // 점수 = 버틴 시간(초)
  gameScore = (now - gameStartMs) / 1000;

  // 자이로 → 이동
  float pitch = ypr[1] * 180 / M_PI - yprOffset[1];
  float roll  = ypr[2] * 180 / M_PI - yprOffset[2];

  const float SENS = 0.12;
  planeX += roll  * SENS;
  planeY += pitch * SENS;

  // 경계 처리
  if (planeX < 4)   planeX = 4;
  if (planeX > 123) planeX = 123;
  if (planeY < 4)   planeY = 4;
  if (planeY > 59)  planeY = 59;

  // 장애물 스폰 (점수 오르면 자주)
  int spawnRate = 40 - min(25, gameScore / 2);
  if (random(0, spawnRate) == 0) spawnObstacle();

  // 장애물 업데이트 & 충돌
  for (int i = 0; i < MAX_OBS; i++) {
    if (!obstacles[i].active) continue;
    obstacles[i].y += obstacles[i].vy * dt * 30;

    if (obstacles[i].y > 70) {
      obstacles[i].active = false;
      continue;
    }

    float dx = obstacles[i].x - planeX;
    float dy = obstacles[i].y - planeY;
    if (dx * dx + dy * dy < (obstacles[i].r + 4) * (obstacles[i].r + 4)) {
      gameOver = true;
      gameOverMs = now;
      return;
    }
  }

  // ---- 렌더 ----
  display.clearDisplay();

  // 별
  for (int i = 0; i < STAR_COUNT; i++) {
    display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
  }

  // 장애물 (운석)
  for (int i = 0; i < MAX_OBS; i++) {
    if (!obstacles[i].active) continue;
    int ox = (int)obstacles[i].x;
    int oy = (int)obstacles[i].y;
    display.fillCircle(ox, oy, obstacles[i].r, SSD1306_WHITE);
    display.drawPixel(ox - obstacles[i].r - 1, oy, SSD1306_WHITE);
    display.drawPixel(ox + obstacles[i].r + 1, oy + 1, SSD1306_WHITE);
    display.drawPixel(ox, oy + obstacles[i].r + 1, SSD1306_WHITE);
  }

  // 비행기
  drawPlane((int)planeX, (int)planeY);

  // 점수
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("S:"));
  display.print(gameScore);

  display.display();
  digitalWrite(LED_PIN, (now / 500) % 2);
}

void drawPlane(int x, int y) {
  display.drawLine(x, y - 5, x, y + 4, SSD1306_WHITE);
  display.drawLine(x - 4, y + 1, x + 4, y + 1, SSD1306_WHITE);
  display.drawLine(x - 2, y + 4, x + 2, y + 4, SSD1306_WHITE);
  display.drawPixel(x - 1, y - 4, SSD1306_WHITE);
  display.drawPixel(x + 1, y - 4, SSD1306_WHITE);
  display.drawPixel(x, y - 5, SSD1306_WHITE);
}