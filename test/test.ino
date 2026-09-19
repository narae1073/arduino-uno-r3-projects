#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== START ===");

  // 1) SCL/SDA를 GPIO로 만들어서 버스 강제 리셋
  pinMode(A5, OUTPUT);   // SCL
  pinMode(A4, OUTPUT);   // SDA
  digitalWrite(A4, HIGH);
  digitalWrite(A5, HIGH);
  delay(10);

  // SCL을 9번 토글 (stuck 슬레이브 해제)
  for (int i = 0; i < 9; i++) {
    digitalWrite(A5, LOW);
    delayMicroseconds(10);
    digitalWrite(A5, HIGH);
    delayMicroseconds(10);
  }
  // STOP 조건 생성
  digitalWrite(A4, LOW);
  delayMicroseconds(10);
  digitalWrite(A5, HIGH);
  delayMicroseconds(10);
  digitalWrite(A4, HIGH);
  delayMicroseconds(10);

  Serial.println("Bus reset done");

  // 2) Wire 시작
  Wire.begin();
  Wire.setClock(100000);
  delay(100);

  // 3) 스캔
  Serial.println("Scanning...");
  for (byte a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found 0x");
      Serial.println(a, HEX);
    }
  }

  // 4) begin 시도
  Serial.println("Calling begin()");
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("FAIL");
    for(;;);
  }
  Serial.println("OK");

  display.ssd1306_command(0x81);
  display.ssd1306_command(255);
  display.fillScreen(SSD1306_WHITE);
  display.display();
  Serial.println("Done");
}

void loop() {}