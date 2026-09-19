#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================== 핀 ====================
#define JOY_X_PIN      A0
#define JOY_Y_PIN      A1
#define BUTTON_PIN     4
#define LED_PIN        13

// ==================== OLED ====================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== 조이스틱 ====================
int joyCenterX = 512;
int joyCenterY = 512;
const int DEADZONE = 60;
const float SENS = 0.008;   // 조이스틱 편차(0~512) → 픽셀 변환
const float MAX_STEP = 2.0;

// ==================== 게임 ====================
struct Obstacle {
  float x, y, vy;
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
const unsigned long FRAME_INTERVAL = 33;

struct Star { uint8_t x, y; };
const int STAR_COUNT = 20;
Star stars[STAR_COUNT];

// ==================== 버튼 ====================
int lastButtonReading = HIGH;
unsigned long lastDebounce = 0;
unsigned long buttonPressStart = 0;
bool buttonHeld = false;
const unsigned long DEBOUNCE_MS = 40;
const unsigned long LONG_PRESS_MS = 600;

// ==================== 함수 프로토타입 ====================
void handleButton();
void initGame();
void spawnObstacle();
void updateAndDrawGame();
void drawPlane(int x, int y);
void calibrateJoystick();

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  Wire.begin();
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 fail"));
    for (;;);
  }
  display.ssd1306_command(0x81);
  display.ssd1306_command(255);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Calibrating joy..."));
  display.println(F("Don't touch stick"));
  display.display();

  // 조이스틱 중앙값 캘리브레이션
  calibrateJoystick();

  randomSeed(analogRead(A2));  // A2 사용 (A0/A1은 조이스틱)
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].x = random(0, 128);
    stars[i].y = random(0, 64);
  }

  initGame();
  delay(300);
  Serial.println(F("Setup complete"));
}

// ==================== 조이스틱 캘리브레이션 ====================
void calibrateJoystick() {
  long sumX = 0, sumY = 0;
  const int N = 20;
  for (int i = 0; i < N; i++) {
    sumX += analogRead(JOY_X_PIN);
    sumY += analogRead(JOY_Y_PIN);
    delay(10);
  }
  joyCenterX = sumX / N;
  joyCenterY = sumY / N;
  Serial.print(F("Center X="));
  Serial.print(joyCenterX);
  Serial.print(F(" Y="));
  Serial.println(joyCenterY);
}

// ==================== loop ====================
void loop() {
  handleButton();
  updateAndDrawGame();
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
        if (buttonHeld) {
          unsigned long held = millis() - buttonPressStart;
          if (held < LONG_PRESS_MS) {
            initGame();
            Serial.println(F("Reset game"));
          }
          buttonHeld = false;
        }
      }
    }
  }
  lastButtonReading = reading;

  // 길게 누름 → 조이스틱 재캘리브레이션
  if (buttonHeld && (millis() - buttonPressStart >= LONG_PRESS_MS)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("Recalibrating..."));
    display.println(F("Release stick"));
    display.display();
    delay(500);
    calibrateJoystick();
    initGame();
    Serial.println(F("Recalibrated"));
    buttonHeld = false;
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

  if (now - lastFrame < FRAME_INTERVAL) return;
  float dt = (now - lastFrame) / 1000.0;
  lastFrame = now;

  gameScore = (now - gameStartMs) / 1000;

  // ---- 조이스틱 읽기 ----
  int jx = analogRead(JOY_X_PIN) - joyCenterX;
  int jy = analogRead(JOY_Y_PIN) - joyCenterY;

  // 데드존
  if (abs(jx) < DEADZONE) jx = 0;
  if (abs(jy) < DEADZONE) jy = 0;

  // 이동량 계산
  float dx = jx * SENS;
  float dy = jy * SENS;

  // 스텝 제한
  if (dx >  MAX_STEP) dx =  MAX_STEP;
  if (dx < -MAX_STEP) dx = -MAX_STEP;
  if (dy >  MAX_STEP) dy =  MAX_STEP;
  if (dy < -MAX_STEP) dy = -MAX_STEP;

  // ★ 조이스틱 Y축 방향이 반대면 부호 바꿔야 함
  planeX += dx;
  planeY += dy;

  // 경계 처리
  if (planeX < 4)   planeX = 4;
  if (planeX > 123) planeX = 123;
  if (planeY < 4)   planeY = 4;
  if (planeY > 59)  planeY = 59;

  // 디버그
  static unsigned long lastDbg = 0;
  if (millis() - lastDbg > 300) {
    lastDbg = millis();
    Serial.print(F("jx=")); Serial.print(jx);
    Serial.print(F(" jy=")); Serial.print(jy);
    Serial.print(F(" X=")); Serial.print(planeX);
    Serial.print(F(" Y=")); Serial.println(planeY);
  }

  // 장애물 스폰
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

    float ddx = obstacles[i].x - planeX;
    float ddy = obstacles[i].y - planeY;
    if (ddx * ddx + ddy * ddy < (obstacles[i].r + 4) * (obstacles[i].r + 4)) {
      gameOver = true;
      gameOverMs = now;
      return;
    }
  }

  // 렌더
  display.clearDisplay();

  for (int i = 0; i < STAR_COUNT; i++) {
    display.drawPixel(stars[i].x, stars[i].y, SSD1306_WHITE);
  }

  for (int i = 0; i < MAX_OBS; i++) {
    if (!obstacles[i].active) continue;
    int ox = (int)obstacles[i].x;
    int oy = (int)obstacles[i].y;
    display.fillCircle(ox, oy, obstacles[i].r, SSD1306_WHITE);
    display.drawPixel(ox - obstacles[i].r - 1, oy, SSD1306_WHITE);
    display.drawPixel(ox + obstacles[i].r + 1, oy + 1, SSD1306_WHITE);
    display.drawPixel(ox, oy + obstacles[i].r + 1, SSD1306_WHITE);
  }

  drawPlane((int)planeX, (int)planeY);

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