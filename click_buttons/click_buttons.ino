// Кликер для ESP32-S3 (YD-ESP32-23): 4 кнопки Kailh → левая кнопка мыши, у каждой кнопки своя лампочка.
// Плата видна компьютеру как USB-мышь через разъём «USB» (не «COM»).
//
// Проводка каждой кнопки (у кнопки и лампочки по два усика, один усик кнопки скручен с усиком лампочки):
//   скрутка (общая точка)  → GND платы (минусовая шина макетки)
//   свободный усик кнопки  → BTN_PINS[i]
//   свободный усик лампочки → LED_PINS[i]
// Если клики работают, а лампочки не горят — лампочка стоит «наоборот»: скрутки перенести с GND на 3V3
// и поставить COMMON_TO_GND = false.

#include "USB.h"
#include "USBHIDMouse.h"
#include "driver/gpio.h"

const bool COMMON_TO_GND = true;

const uint8_t N = 4;
const uint8_t BTN_PINS[N] = {4, 5, 6, 7};
const uint8_t LED_PINS[N] = {15, 16, 17, 18};

const uint32_t DEBOUNCE_MS = 5;  // у Kailh дребезг короткий; больше — медленнее предел кликов

USBHIDMouse Mouse;

bool pressed[N];
uint32_t changedAt[N];

bool readPressed(uint8_t i) {
  int level = digitalRead(BTN_PINS[i]);
  return COMMON_TO_GND ? level == LOW : level == HIGH;
}

void setLed(uint8_t i, bool on) {
  // общая точка на GND — лампочка горит от HIGH; на 3V3 — от LOW
  digitalWrite(LED_PINS[i], (on == COMMON_TO_GND) ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < N; i++) {
    pinMode(BTN_PINS[i], COMMON_TO_GND ? INPUT_PULLUP : INPUT_PULLDOWN);
    pinMode(LED_PINS[i], OUTPUT);
    // Лампочки брелока рассчитаны на батарейку и стоят без резистора:
    // самый слабый выход (~5 мА) ограничивает ток вместо него.
    gpio_set_drive_capability((gpio_num_t)LED_PINS[i], GPIO_DRIVE_CAP_0);
    setLed(i, false);
    pressed[i] = false;
    changedAt[i] = 0;
  }
  Mouse.begin();
  USB.begin();
  Serial.println("clicker: started");
}

void loop() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < N; i++) {
    bool p = readPressed(i);
    if (p == pressed[i] || now - changedAt[i] < DEBOUNCE_MS) continue;
    pressed[i] = p;
    changedAt[i] = now;
    setLed(i, p);
    if (Serial) Serial.printf("button %u %s\n", i + 1, p ? "down" : "up");

    if (p) {
      // новое нажатие — всегда новый клик, даже если другая кнопка ещё зажата
      if (Mouse.isPressed(MOUSE_LEFT)) Mouse.release(MOUSE_LEFT);
      Mouse.press(MOUSE_LEFT);
    } else {
      bool anyHeld = false;
      for (uint8_t j = 0; j < N; j++) anyHeld |= pressed[j];
      if (!anyHeld) Mouse.release(MOUSE_LEFT);  // удержание = зажатая ЛКМ, можно перетаскивать
    }
  }
}
