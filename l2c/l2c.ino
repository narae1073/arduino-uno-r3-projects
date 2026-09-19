#include <Wire.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 display(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin();
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("fail");
    for(;;);
  }
  Serial.println("begin ok");

  // ★ 대비 최대 (0x81 = SETCONTRAST 명령)
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(255);

  display.fillScreen(SSD1306_WHITE);   // 전체 흰색
  display.display();
  Serial.println("filled white");
}

void loop() {}