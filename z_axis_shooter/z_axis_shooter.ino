#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_ADDRESS 0x3C
// ==================== 핀 ====================
#define JOY_X_PIN      A0
#define JOY_Y_PIN      A1
#define BUTTON_PIN     4
#define LED_PIN        13

// ==================== OLED ====================
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ==================== 조이스틱 ====================
int joyCenterX = 512, joyCenterY = 512;

// ==================== 3D 월드 ====================
const float FOCAL = 80.0;         // 원근 계수 (작을수록 광각)
const float FAR_Z = 300.0;        // 스폰 거리
const float NEAR_Z = 12.0;        // 충돌 판정 거리
const float CAM_RANGE = 60.0;     // 카메라 이동 범위

float camX = 0, camY = 0;         // 카메라(비행기) 월드 좌표

// 적
struct Enemy {
  float wx, wy, z;
  float r;
  bool active;
};
const int MAX_ENEMIES = 4;
const int STAR_COUNT = 15;

Enemy enemies[MAX_ENEMIES];

// 별 (배경)
struct Star { float wx, wy, z; };
Star stars[STAR_COUNT];

// ==================== 게임 상태 ====================
unsigned long gameStartMs;
int gameScore;
bool gameOver;
unsigned long gameOverMs;
unsigned long lastFrame = 0;
const unsigned long FRAME_INTERVAL = 33;   // ~30fps

// ==================== 버튼 ====================
int lastButtonReading = HIGH;
unsigned long lastDebounce = 0;
unsigned long buttonPressStart = 0;
bool buttonHeld = false;

// ==================== 함수 프로토타입 ====================
void calibrateJoystick();
void initGame();
void spawnEnemy();
void handleButton();
void drawShip();
bool project(float wx, float wy, float z, float r, int &sx, int &sy, int &sr);

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("=== START ==="));

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  Serial.println(F("Pins set"));

  Wire.begin();
  Wire.setClock(100000);
  Serial.println(F("Wire begun"));

  Serial.println(F("Calling display.begin()"));
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 fail"));
    for (;;);
  }
  Serial.println(F("display OK"));

  display.ssd1306_command(0x81);
  display.ssd1306_command(255);
  Serial.println(F("Contrast set"));

  // ... 나머지

  display.ssd1306_command(0x81);
  display.ssd1306_command(255);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Calibrating..."));
  display.println(F("Don't touch stick"));
  display.display();

  calibrateJoystick();

  // 렌더링 속도 위해 400kHz로
  Wire.setClock(400000);

  randomSeed(analogRead(A2));

  // 별 초기화
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].wx = random(-300, 300);
    stars[i].wy = random(-200, 200);
    stars[i].z = random(20, 300);
  }

  initGame();
  delay(300);
  Serial.println(F("Setup complete"));
}

// ==================== 조이스틱 캘리브레이션 ====================
void calibrateJoystick() {
  delay(300);
  long sx = 0, sy = 0;
  for (int i = 0; i < 30; i++) {
    sx += analogRead(JOY_X_PIN);
    sy += analogRead(JOY_Y_PIN);
    delay(5);
  }
  joyCenterX = sx / 30;
  joyCenterY = sy / 30;
  Serial.print(F("Center X=")); Serial.print(joyCenterX);
  Serial.print(F(" Y=")); Serial.println(joyCenterY);
}

// ==================== 게임 초기화 ====================
void initGame() {
  camX = 0; camY = 0;
  gameStartMs = millis();
  gameScore = 0;
  gameOver = false;
  for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
  lastFrame = millis();
}

void spawnEnemy() {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) {
      enemies[i].active = true;
      enemies[i].wx = random(-80, 80);
      enemies[i].wy = random(-50, 50);
      enemies[i].z = FAR_Z;
      enemies[i].r = random(4, 9);
      return;
    }
  }
}

// ==================== 원근 투영 ====================
// 월드 좌표 (wx, wy, z) → 화면 좌표 (sx, sy)와 화면 반지름 sr
bool project(float wx, float wy, float z, float r, int &sx, int &sy, int &sr) {
  if (z < 1.0) return false;

  sx = 64 + (int)((wx - camX) * FOCAL / z);
  sy = 32 + (int)((wy - camY) * FOCAL / z);
  sr = (int)(r * FOCAL / z);
  if (sr < 1) sr = 1;
  if (sr > 20) sr = 20;   // 그리기 부담 방지
  return true;
}

// ==================== 버튼 ====================
void handleButton() {
  int reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonReading) lastDebounce = millis();

  if ((millis() - lastDebounce) > 40) {
    static int stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        buttonPressStart = millis();
        buttonHeld = true;
      } else {
        if (buttonHeld && (millis() - buttonPressStart) < 600) {
          initGame();
          Serial.println(F("Game reset"));
        }
        buttonHeld = false;
      }
    }
  }
  lastButtonReading = reading;

  // 길게 → 재캘리브레이션
  if (buttonHeld && (millis() - buttonPressStart >= 600)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("Recalibrating..."));
    display.println(F("Release stick"));
    display.display();
    delay(500);
    calibrateJoystick();
    initGame();
    buttonHeld = false;
  }
}

// ==================== 비행기 (십자선) ====================
void drawShip() {
  display.drawCircle(64, 32, 6, SSD1306_WHITE);
  display.drawPixel(64, 32, SSD1306_WHITE);
  display.drawLine(64, 26, 64, 22, SSD1306_WHITE);
  display.drawLine(64, 38, 64, 42, SSD1306_WHITE);
  display.drawLine(58, 32, 54, 32, SSD1306_WHITE);
  display.drawLine(70, 32, 74, 32, SSD1306_WHITE);
}

// ==================== loop ====================
void loop() {
  handleButton();
  unsigned long now = millis();

  // 게임오버
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

  gameScore = (now - gameStartMs) / 1000;

  // ---- 조이스틱으로 카메라 이동 ----
  int jx = analogRead(JOY_X_PIN) - joyCenterX;
  int jy = analogRead(JOY_Y_PIN) - joyCenterY;
  if (abs(jx) < 60) jx = 0;
  if (abs(jy) < 60) jy = 0;

  const float SENS = 0.06;
  camX += jx * SENS;
  camY += jy * SENS;

  if (camX < -CAM_RANGE) camX = -CAM_RANGE;
  if (camX >  CAM_RANGE) camX =  CAM_RANGE;
  if (camY < -CAM_RANGE) camY = -CAM_RANGE;
  if (camY >  CAM_RANGE) camY =  CAM_RANGE;

  // ---- 적 스폰 (점수 오르면 자주) ----
  int spawnRate = 30 - min(20, gameScore / 2);
  if (random(0, spawnRate) == 0) spawnEnemy();

  // ---- 적 업데이트 ----
  float speed = 60.0 + min(80.0, gameScore * 2.0);

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!enemies[i].active) continue;
    enemies[i].z -= speed * dt;

    if (enemies[i].z < NEAR_Z) {
      float ddx = enemies[i].wx - camX;
      float ddy = enemies[i].wy - camY;
      float hitR = enemies[i].r + 8.0;
      if (ddx * ddx + ddy * ddy < hitR * hitR) {
        gameOver = true;
        gameOverMs = now;
        return;
      }
      enemies[i].active = false;
    }
  }

  // ---- 별 업데이트 ----
  for (int i = 0; i < STAR_COUNT; i++) {
    stars[i].z -= speed * 0.9 * dt;
    if (stars[i].z < 5) {
      stars[i].wx = random(-300, 300);
      stars[i].wy = random(-200, 200);
      stars[i].z = FAR_Z;
    }
  }

  // ==================== 렌더 ====================
  display.clearDisplay();

  // 별 (뒤에 있는 것부터)
  for (int i = 0; i < STAR_COUNT; i++) {
    int sx, sy, sr;
    if (project(stars[i].wx, stars[i].wy, stars[i].z, 0.6, sx, sy, sr)) {
      if (sx >= 0 && sx < 128 && sy >= 0 && sy < 64) {
        display.drawPixel(sx, sy, SSD1306_WHITE);
      }
    }
  }

  // 적 (먼 것부터 그려서 가까운 게 위에 오게)
  int order[MAX_ENEMIES];
  int cnt = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (enemies[i].active) order[cnt++] = i;
  }
  // z 내림차순 정렬 (먼 것 먼저)
  for (int i = 0; i < cnt - 1; i++) {
    for (int j = 0; j < cnt - 1 - i; j++) {
      if (enemies[order[j]].z < enemies[order[j + 1]].z) {
        int t = order[j]; order[j] = order[j + 1]; order[j + 1] = t;
      }
    }
  }

  for (int k = 0; k < cnt; k++) {
    int i = order[k];
    int sx, sy, sr;
    if (project(enemies[i].wx, enemies[i].wy, enemies[i].z, enemies[i].r, sx, sy, sr)) {
      // 화면 밖이면 스킵
      if (sx + sr < 0 || sx - sr >= 128 || sy + sr < 0 || sy - sr >= 64) continue;
      display.fillCircle(sx, sy, sr, SSD1306_WHITE);
      // 운석 느낌: 표면 디테일
      if (sr >= 4) {
        display.drawPixel(sx - sr / 2, sy - sr / 2, SSD1306_BLACK);
        display.drawPixel(sx + sr / 2, sy + sr / 3, SSD1306_BLACK);
      }
    }
  }

  // 비행기 십자선
  drawShip();

  // 점수
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("S:"));
  display.print(gameScore);

  display.display();
  digitalWrite(LED_PIN, (now / 500) % 2);
}