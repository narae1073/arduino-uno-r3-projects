#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define JOY_X_PIN   A0
#define JOY_Y_PIN   A1
#define BUTTON_PIN  4
#define LED_PIN     13

Adafruit_SSD1306 display(128, 64, &Wire, -1);

int joyCenterX = 512, joyCenterY = 512;

// 카메라(터널 중앙 기준 상대 위치)
float playerX = 0, playerY = 0;
float worldDist = 0;              // 진행 거리 (경로 파라미터)

const float TUNNEL_HALF = 35.0;
const float CAM_RANGE   = 45.0;
const float FOCAL       = 80.0;

// ★ 커브 경로 설정
const float PATH_AMP  = 50.0;     // 진폭: 얼마나 휘는지 (0이면 직선)
const float PATH_FREQ = 0.008;    // 주파수: 얼마나 자주 휘는지

float pathX(float t) {
  return PATH_AMP * sin(t * PATH_FREQ);
}
float pathY(float t) {
  return PATH_AMP * 0.6 * sin(t * PATH_FREQ * 0.75 + 2.0);
}

// 링
const int RING_COUNT = 7;
const float RING_SPACING = 60.0;
const float RING_START   = 40.0;
struct Ring { float d; };
Ring rings[RING_COUNT];

// 큐브
struct Cube {
  float wx, wy, d;
  float size;
  bool active;
};
const int MAX_CUBES = 6;
Cube cubes[MAX_CUBES];

// ★ 함수 프로토타입 명시
void calibrateJoystick();
void initGame();
void spawnCube();
void drawRing(float d);
void drawCube(Cube &c);
void drawCrosshair();
void handleButton();

unsigned long gameStartMs;
int gameScore;
bool gameOver;
unsigned long gameOverMs;
unsigned long lastFrame = 0;
const unsigned long FRAME_INTERVAL = 40;

int lastButtonReading = HIGH;
unsigned long lastDebounce = 0;
unsigned long buttonPressStart = 0;
bool buttonHeld = false;

// ==================== 조이스틱 ====================
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
}

// ==================== 초기화 ====================
void initGame() {
  playerX = 0; playerY = 0;
  worldDist = 0;
  gameStartMs = millis();
  gameScore = 0;
  gameOver = false;
  for (int i = 0; i < RING_COUNT; i++) {
    rings[i].d = RING_START + i * RING_SPACING;
  }
  for (int i = 0; i < MAX_CUBES; i++) cubes[i].active = false;
  lastFrame = millis();
}

void spawnCube() {
  for (int i = 0; i < MAX_CUBES; i++) {
    if (!cubes[i].active) {
      cubes[i].active = true;
      cubes[i].wx = random(-20, 20);
      cubes[i].wy = random(-20, 20);
      cubes[i].d  = RING_START + RING_COUNT * RING_SPACING;
      cubes[i].size = random(3, 6);
      return;
    }
  }
}

// ==================== 링 그리기 ====================
void drawRing(float d) {
  if (d < 4.0) return;

  // 이 링의 월드 진행 거리 = 현재 카메라 + 앞쪽 거리
  float ringWorldDist = worldDist + d;

  // 경로에 의한 상대 오프셋
  float relX = pathX(ringWorldDist) - pathX(worldDist) - playerX;
  float relY = pathY(ringWorldDist) - pathY(worldDist) - playerY;

  float h = TUNNEL_HALF;
  float wx[4] = {-h,  h,  h, -h};
  float wy[4] = {-h, -h,  h,  h};

  int sx[4], sy[4];
  for (int i = 0; i < 4; i++) {
    sx[i] = 64 + (int)((relX + wx[i]) * FOCAL / d);
    sy[i] = 32 + (int)((relY + wy[i]) * FOCAL / d);
    if (sx[i] < -300 || sx[i] > 428 || sy[i] < -300 || sy[i] > 364) return;
  }
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    display.drawLine(sx[i], sy[i], sx[j], sy[j], SSD1306_WHITE);
  }
}

// ==================== 큐브 그리기 ====================
void drawCube(Cube &c) {
  float cubeWorldDist = worldDist + c.d;
  float relX = pathX(cubeWorldDist) - pathX(worldDist) - playerX + c.wx;
  float relY = pathY(cubeWorldDist) - pathY(worldDist) - playerY + c.wy;

  float h = c.size;
  float ox[8] = {-h,  h,  h, -h, -h,  h,  h, -h};
  float oy[8] = {-h, -h,  h,  h, -h, -h,  h,  h};
  float od[8] = {-h, -h, -h, -h,  h,  h,  h,  h};

  int sx[8], sy[8];
  for (int i = 0; i < 8; i++) {
    float dd = c.d + od[i];
    if (dd < 4.0) return;
    sx[i] = 64 + (int)((relX + ox[i]) * FOCAL / dd);
    sy[i] = 32 + (int)((relY + oy[i]) * FOCAL / dd);
  }
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    display.drawLine(sx[i], sy[i], sx[j], sy[j], SSD1306_WHITE);
    display.drawLine(sx[i+4], sy[i+4], sx[j+4], sy[j+4], SSD1306_WHITE);
    display.drawLine(sx[i], sy[i], sx[i+4], sy[i+4], SSD1306_WHITE);
  }
}

// ==================== 조준선 ====================
void drawCrosshair() {
  display.drawCircle(64, 32, 5, SSD1306_WHITE);
  display.drawPixel(64, 32, SSD1306_WHITE);
  display.drawLine(64, 26, 64, 22, SSD1306_WHITE);
  display.drawLine(64, 38, 64, 42, SSD1306_WHITE);
  display.drawLine(58, 32, 54, 32, SSD1306_WHITE);
  display.drawLine(70, 32, 74, 32, SSD1306_WHITE);
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
        if (buttonHeld && (millis() - buttonPressStart) < 600) initGame();
        buttonHeld = false;
      }
    }
  }
  lastButtonReading = reading;

  if (buttonHeld && (millis() - buttonPressStart >= 600)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(F("Recalibrating..."));
    display.display();
    delay(500);
    calibrateJoystick();
    initGame();
    buttonHeld = false;
  }
}

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
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
  display.setCursor(0, 0);
  display.println(F("Calibrating..."));
  display.display();

  calibrateJoystick();
  randomSeed(analogRead(A2));
  Wire.setClock(400000);
  initGame();
  delay(300);
}

// ==================== loop ====================
void loop() {
  handleButton();
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
    display.display();
    if (now - gameOverMs > 2000) initGame();
    return;
  }

  if (now - lastFrame < FRAME_INTERVAL) return;
  float dt = (now - lastFrame) / 1000.0;
  lastFrame = now;
  gameScore = (now - gameStartMs) / 1000;

  // 조이스틱 → 카메라(플레이어) 위치
  int jx = analogRead(JOY_X_PIN) - joyCenterX;
  int jy = analogRead(JOY_Y_PIN) - joyCenterY;
  if (abs(jx) < 80) jx = 0;
  if (abs(jy) < 80) jy = 0;

  const float SENS = 0.007;
  playerX += jx * SENS;
  playerY += jy * SENS;

  if (playerX < -CAM_RANGE || playerX > CAM_RANGE ||
      playerY < -CAM_RANGE || playerY > CAM_RANGE) {
    playerX = constrain(playerX, -CAM_RANGE, CAM_RANGE);
    playerY = constrain(playerY, -CAM_RANGE, CAM_RANGE);
    gameOver = true;
    gameOverMs = now;
    return;
  }

  // 전진
  float speed = 90.0 + min(120.0, gameScore * 3.0);
  worldDist += speed * dt;

  // 링 업데이트
  for (int i = 0; i < RING_COUNT; i++) {
    rings[i].d -= speed * dt;
    if (rings[i].d < 5) rings[i].d += RING_COUNT * RING_SPACING;
  }

  // 큐브 스폰
  int spawnRate = 40 - min(25, gameScore);
  if (random(0, spawnRate) == 0) spawnCube();

  // 큐브 업데이트 & 충돌
  for (int i = 0; i < MAX_CUBES; i++) {
    if (!cubes[i].active) continue;
    cubes[i].d -= speed * dt;
    if (cubes[i].d < 5) {
      float cubeWorldDist = worldDist + cubes[i].d;
      float dx = pathX(cubeWorldDist) - pathX(worldDist) + cubes[i].wx - playerX;
      float dy = pathY(cubeWorldDist) - pathY(worldDist) + cubes[i].wy - playerY;
      float hitR = cubes[i].size ;
      if (dx*dx + dy*dy < hitR*hitR) {
        gameOver = true;
        gameOverMs = now;
        return;
      }
      cubes[i].active = false;
    }
  }

  // ==================== 렌더 ====================
  display.clearDisplay();

  int order[RING_COUNT];
  for (int i = 0; i < RING_COUNT; i++) order[i] = i;
  for (int i = 0; i < RING_COUNT - 1; i++) {
    for (int j = 0; j < RING_COUNT - 1 - i; j++) {
      if (rings[order[j]].d < rings[order[j+1]].d) {
        int t = order[j]; order[j] = order[j+1]; order[j+1] = t;
      }
    }
  }
  for (int i = 0; i < RING_COUNT; i++) drawRing(rings[order[i]].d);

  for (int i = 0; i < MAX_CUBES; i++) {
    if (cubes[i].active) drawCube(cubes[i]);
  }

  // drawCrosshair();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("S:"));
  display.print(gameScore);
  display.display();
  digitalWrite(LED_PIN, (now / 500) % 2);
}