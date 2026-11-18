// Проверка проводки кликера без USB-мыши: печатает в порт COM (115200) уровни кнопок
// и мигает лампочками раз в секунду. Ничего не кликает.
#include "driver/gpio.h"

const uint8_t BTN_PINS[] = {4, 5, 6, 7};
const uint8_t LED_PINS[] = {15, 16, 17, 18};

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    pinMode(LED_PINS[i], OUTPUT);
    gpio_set_drive_capability((gpio_num_t)LED_PINS[i], GPIO_DRIVE_CAP_0);
  }
}

void loop() {
  static uint32_t n = 0;
  bool ledOn = (millis() / 1000) % 2;
  for (uint8_t i = 0; i < 4; i++) digitalWrite(LED_PINS[i], ledOn);
  delay(2);  // дать уровню установиться после переключения лампочек
  // 1 = отпущена, 0 = нажата. Если btn повторяет led — вход кнопки замкнут на провод лампочки.
  int b[4];
  for (uint8_t i = 0; i < 4; i++) b[i] = digitalRead(BTN_PINS[i]);
  Serial.printf("%6u led=%d  btn4=%d btn5=%d btn6=%d btn7=%d\n", n++, ledOn, b[0], b[1], b[2], b[3]);
  delay(100);
}
