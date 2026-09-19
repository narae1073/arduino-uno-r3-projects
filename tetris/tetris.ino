#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/pgmspace.h>

#define JOY_X_PIN       A0
#define JOY_Y_PIN       A1
#define JOY_SW_PIN      7
#define SCREEN_BTN_PIN  4
#define LED_PIN         13

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ==================== 화면 모드 ====================
int screenMode = 0;
int lastScreenReading = HIGH;
unsigned long screenDebounceTime = 0;

// ==================== 테트리스 ====================
const int COLS = 10;
const int ROWS = 20;
const int CELL = 3;
const int BOARD_X = 2;
const int BOARD_Y = 2;

// ★ board 비트 패킹: 각 행이 uint16_t, 하위 10비트 사용 → 40바이트
uint16_t board[ROWS];

int joyCenterX = 512, joyCenterY = 512;

// ★ PIECES를 PROGMEM으로 → RAM에서 제외
const uint16_t PIECES[7][4] PROGMEM = {
  {0x0F00, 0x2222, 0x00F0, 0x4444},  // I
  {0x6600, 0x6600, 0x6600, 0x6600},  // O
  {0x4E00, 0x4640, 0x0E40, 0x4C40},  // T
  {0x6C00, 0x4620, 0x06C0, 0x8C40},  // S
  {0xC600, 0x2640, 0x0C60, 0x4C80},  // Z
  {0x8E00, 0x6440, 0x0E20, 0x44C0},  // J
  {0x2E00, 0x4460, 0x0E80, 0xC440}   // L
};

struct Piece {
  int8_t type;
  int8_t rot;
  int8_t x;
  int8_t y;
};
Piece curPiece;
int8_t nextType;

unsigned long lastFall = 0;
unsigned long fallInterval = 800;
unsigned long lastJoyMove = 0;
unsigned long lastRepeat = 0;

bool prevLeft = false, prevRight = false;
bool prevDown = false, prevUp = false, prevBtn = false;

int score = 0;
int lines = 0;
int8_t level = 1;
bool gameOver = false;
unsigned long gameOverMs = 0;

// ==================== 시계 ====================
unsigned long clockBase = 12UL * 3600UL;
unsigned long lastResync = 0;
unsigned long lastClockAdjust = 0;

// ==================== 보드 접근 함수 (비트 패킹) ====================
inline uint8_t getCell(int r, int c) {
  return (board[r] >> c) & 1;
}

inline void setCell(int r, int c, uint8_t v) {
  if (v) board[r] |= (1 << c);
  else   board[r] &= ~(1 << c);
}

inline void clearRow(int r) { board[r] = 0; }
inline void copyRow(int dst, int src) { board[dst] = board[src]; }

// PROGMEM 접근
inline uint16_t getPieceMask(int type, int rot) {
  return pgm_read_word(&PIECES[type][rot]);
}

// ==================== 프로토타입 ====================
void calibrateJoystick();
void initGame();
void spawnPiece();
bool checkCollision(int type, int rot, int px, int py);
void lockPiece();
void clearLines();
void drawBlock(int bx, int by);
void drawBoard();
void drawCurrentPiece();
void drawNextPiece();
void drawUI();
void render();
void handleTetrisInput();
void handleScreenButton();
void adjustClock(long deltaSeconds);
void handleClockInput();
void drawClockScreen();
bool getBit(uint16_t mask, int r, int c);

// ==================== setup ====================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  pinMode(SCREEN_BTN_PIN, INPUT_PULLUP);
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
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Calibrating..."));
  display.println(F("Don't touch stick"));
  display.display();

  calibrateJoystick();

  Wire.setClock(400000);

  initGame();
  lastResync = millis();
  lastFall = millis();
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
}

// ==================== 화면 전환 ====================
void handleScreenButton() {
  int reading = digitalRead(SCREEN_BTN_PIN);
  unsigned long now = millis();

  if (reading != lastScreenReading) screenDebounceTime = now;

  if (now - screenDebounceTime > 50) {
    static int stableState = HIGH;
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        screenMode = (screenMode + 1) % 2;
        if (screenMode == 1) {
          lastFall = millis();
          prevLeft = prevRight = prevDown = prevUp = prevBtn = false;
        }
      }
    }
  }
  lastScreenReading = reading;
}

// ==================== 시계 조정 ====================
void adjustClock(long deltaSeconds) {
  unsigned long elapsed = (millis() - lastResync) / 1000;
  long total = (long)clockBase + (long)elapsed + deltaSeconds;
  if (total < 0) total += 86400L;
  total %= 86400L;
  clockBase = (unsigned long)total;
  lastResync = millis();
}

void handleClockInput() {
  int jx = analogRead(JOY_X_PIN) - joyCenterX;
  int jy = analogRead(JOY_Y_PIN) - joyCenterY;
  const int DEAD = 200;

  unsigned long now = millis();
  if (now - lastClockAdjust < 200) return;

  bool changed = false;
  if (jx < -DEAD) { adjustClock(3600);  changed = true; }
  if (jx >  DEAD) { adjustClock(-3600); changed = true; }
  if (jy < -DEAD) { adjustClock(60);    changed = true; }
  if (jy >  DEAD) { adjustClock(-60);   changed = true; }

  if (changed) lastClockAdjust = now;
}

// ==================== 시계 그리기 ====================
void drawClockScreen() {
  display.clearDisplay();

  unsigned long elapsed = (millis() - lastResync) / 1000;
  unsigned long total = (clockBase + elapsed) % 86400UL;
  int h = (total / 3600) % 24;
  int m = (total / 60) % 60;
  int s = total % 60;

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(F("== CLOCK =="));

  char buf[12];
  sprintf(buf, "%02d:%02d:%02d", h, m, s);
  display.setTextSize(2);
  display.setCursor(16, 22);
  display.print(buf);

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print(F("Stick: +/- time"));

  display.display();
}

// ==================== 테트리스 초기화 ====================
void initGame() {
  for (int r = 0; r < ROWS; r++) board[r] = 0;

  score = 0;
  lines = 0;
  level = 1;
  fallInterval = 800;
  gameOver = false;

  randomSeed(millis());
  nextType = random(0, 7);
  spawnPiece();
  lastFall = millis();
}

void spawnPiece() {
  curPiece.type = nextType;
  curPiece.rot = 0;
  curPiece.x = COLS / 2 - 2;
  curPiece.y = 0;
  nextType = random(0, 7);

  if (checkCollision(curPiece.type, curPiece.rot, curPiece.x, curPiece.y)) {
    gameOver = true;
    gameOverMs = millis();
  }
}

bool getBit(uint16_t mask, int r, int c) {
  return (mask >> (15 - (r * 4 + c))) & 1;
}

bool checkCollision(int type, int rot, int px, int py) {
  uint16_t mask = getPieceMask(type, rot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getBit(mask, r, c)) continue;
      int bx = px + c;
      int by = py + r;
      if (bx < 0 || bx >= COLS) return true;
      if (by >= ROWS) return true;
      if (by < 0) continue;
      if (getCell(by, bx)) return true;
    }
  }
  return false;
}

void lockPiece() {
  uint16_t mask = getPieceMask(curPiece.type, curPiece.rot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getBit(mask, r, c)) continue;
      int bx = curPiece.x + c;
      int by = curPiece.y + r;
      if (by >= 0 && by < ROWS && bx >= 0 && bx < COLS) {
        setCell(by, bx, 1);
      }
    }
  }
  clearLines();
  spawnPiece();
}

void clearLines() {
  int cleared = 0;
  for (int r = ROWS - 1; r >= 0; r--) {
    if (board[r] == 0x3FF) {   // 10비트 전부 1 = 꽉 찬 줄
      for (int rr = r; rr > 0; rr--) copyRow(rr, rr - 1);
      clearRow(0);
      r++;
      cleared++;
    }
  }

  if (cleared > 0) {
    int addScore = 0;
    switch (cleared) {
      case 1: addScore = 100; break;
      case 2: addScore = 300; break;
      case 3: addScore = 500; break;
      case 4: addScore = 800; break;
    }
    score += addScore * level;
    lines += cleared;

    int newLevel = lines / 10 + 1;
    if (newLevel > level) {
      level = newLevel;
      fallInterval = 800 - (level - 1) * 70;
      if (fallInterval < 100) fallInterval = 100;
    }
  }
}

// ==================== 테트리스 입력 ====================
void handleTetrisInput() {
  int jx = analogRead(JOY_X_PIN) - joyCenterX;
  int jy = analogRead(JOY_Y_PIN) - joyCenterY;
  const int DEAD = 80;

  bool left  = (jx < -DEAD);
  bool right = (jx >  DEAD);
  bool down  = (jy >  DEAD);
  bool up    = (jy < -DEAD);
  bool btn   = (digitalRead(JOY_SW_PIN) == LOW);

  unsigned long now = millis();

  if (left && !right) {
    if (!prevLeft) {
      if (!checkCollision(curPiece.type, curPiece.rot, curPiece.x - 1, curPiece.y))
        curPiece.x -= 1;
      lastJoyMove = now;
      lastRepeat = now;
    } else if (now - lastJoyMove > 200 && now - lastRepeat > 80) {
      lastRepeat = now;
      if (!checkCollision(curPiece.type, curPiece.rot, curPiece.x - 1, curPiece.y))
        curPiece.x -= 1;
    }
  } else if (right && !left) {
    if (!prevRight) {
      if (!checkCollision(curPiece.type, curPiece.rot, curPiece.x + 1, curPiece.y))
        curPiece.x += 1;
      lastJoyMove = now;
      lastRepeat = now;
    } else if (now - lastJoyMove > 200 && now - lastRepeat > 80) {
      lastRepeat = now;
      if (!checkCollision(curPiece.type, curPiece.rot, curPiece.x + 1, curPiece.y))
        curPiece.x += 1;
    }
  }

  if (down && now - lastFall > 50) {
    lastFall = now;
    if (!checkCollision(curPiece.type, curPiece.rot, curPiece.x, curPiece.y + 1)) {
      curPiece.y += 1;
      score += 1;
    }
  }

  if (up && !prevUp) {
    int dropped = 0;
    while (!checkCollision(curPiece.type, curPiece.rot, curPiece.x, curPiece.y + 1)) {
      curPiece.y += 1;
      dropped++;
    }
    score += dropped * 2;
    lockPiece();

    prevLeft = left; prevRight = right;
    prevDown = down; prevUp = up; prevBtn = btn;
    return;
  }

  if (btn && !prevBtn) {
    int newRot = (curPiece.rot + 1) % 4;
    if (!checkCollision(curPiece.type, newRot, curPiece.x, curPiece.y)) {
      curPiece.rot = newRot;
    } else if (!checkCollision(curPiece.type, newRot, curPiece.x - 1, curPiece.y)) {
      curPiece.x -= 1; curPiece.rot = newRot;
    } else if (!checkCollision(curPiece.type, newRot, curPiece.x + 1, curPiece.y)) {
      curPiece.x += 1; curPiece.rot = newRot;
    }
  }

  prevLeft = left; prevRight = right;
  prevDown = down; prevUp = up; prevBtn = btn;
}

// ==================== 테트리스 렌더 ====================
void drawBlock(int bx, int by) {
  int px = BOARD_X + bx * CELL;
  int py = BOARD_Y + by * CELL;
  display.fillRect(px, py, CELL - 1, CELL - 1, SSD1306_WHITE);
}

void drawBoard() {
  display.drawRect(BOARD_X - 1, BOARD_Y - 1,
                   COLS * CELL + 1, ROWS * CELL + 1, SSD1306_WHITE);

  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      if (getCell(r, c)) drawBlock(c, r);
    }
  }
}

void drawCurrentPiece() {
  uint16_t mask = getPieceMask(curPiece.type, curPiece.rot);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getBit(mask, r, c)) continue;
      int by = curPiece.y + r;
      int bx = curPiece.x + c;
      if (by >= 0) drawBlock(bx, by);
    }
  }
}

void drawNextPiece() {
  int nx = BOARD_X + COLS * CELL + 8;
  int ny = 24;

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(nx, 4);
  display.print(F("NEXT"));

  uint16_t mask = getPieceMask(nextType, 0);
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!getBit(mask, r, c)) continue;
      display.fillRect(nx + c * CELL, ny + r * CELL, CELL - 1, CELL - 1, SSD1306_WHITE);
    }
  }
}

void drawUI() {
  int nx = BOARD_X + COLS * CELL + 6;

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(nx, 46);
  display.print(F("S:"));
  display.print(score);

  display.setCursor(nx, 56);
  display.print(F("L:"));
  display.print(level);
}

void render() {
  display.clearDisplay();
  drawBoard();
  if (!gameOver) drawCurrentPiece();
  drawNextPiece();
  drawUI();
  display.display();
}

// ==================== loop ====================
void loop() {
  unsigned long now = millis();
  handleScreenButton();

  if (screenMode == 0) {
    handleClockInput();
    drawClockScreen();
    return;
  }

  if (gameOver) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(14, 10);
    display.println(F("GAME OVER"));
    display.setTextSize(1);
    display.setCursor(34, 36);
    display.print(F("Score: "));
    display.print(score);
    display.setCursor(20, 52);
    display.println(F("Resetting..."));
    display.display();

    if (now - gameOverMs > 3000) initGame();
    return;
  }

  handleTetrisInput();
  if (gameOver) return;

  if (now - lastFall > fallInterval) {
    lastFall = now;
    if (!checkCollision(curPiece.type, curPiece.rot, curPiece.x, curPiece.y + 1)) {
      curPiece.y += 1;
    } else {
      lockPiece();
    }
  }

  render();
}