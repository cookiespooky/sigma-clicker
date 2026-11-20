// Кликер для ESP32-S3 (YD-ESP32-23): 4 кнопки Kailh, у каждой своя лампочка и своё назначение.
// Плата видна компьютеру как USB-мышь и USB-клавиатура через разъём «USB» (не «COM»).
// Назначения хранятся во флеше платы (Preferences) и меняются командами через порт отладки — см. bind.sh.
// По умолчанию каждая кнопка — левая кнопка мыши.
// Если при подключении держать любую кнопку, плата ещё и флешка «CLICKER» со страницей назначений bind.htm
// (disk.h); это нажатие ничего не делает на компьютере.
//
// Проводка каждой кнопки (у кнопки и лампочки по два усика, один усик кнопки скручен с усиком лампочки):
//   скрутка (общая точка)  → GND платы (минусовая шина макетки)
//   свободный усик кнопки  → BTN_PINS[i]
//   свободный усик лампочки → LED_PINS[i]
// Если клики работают, а лампочки не горят — лампочка стоит «наоборот»: скрутки перенести с GND на 3V3
// и поставить COMMON_TO_GND = false.
//
// Команды порта (строка, перевод строки в конце; ответ кончается строкой «ok» или «err …»):
//   list                — назначения всех кнопок: «bind 1 mouse left»
//   set N ЗНАЧЕНИЕ      — назначить кнопке N (1–4): none | mouse left|right|middle|back|forward
//                         | key ИМЯ, где ИМЯ — клавиша с модификаторами через «+»: a, f5, ctrl+c, cmd+shift+z, shift
//   reset               — все кнопки обратно в «mouse left»
//   keys                — список имён клавиш
//   learn               — ждать нажатия кнопки (до 30 с): ответ «pressed N»; это нажатие мышью и клавиатурой
//                         не отправляется, только лампочка — чтобы выбрать кнопку, ничего не нажав на компьютере

#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"
#include "Preferences.h"
#include "driver/gpio.h"
#include "disk.h"

const bool COMMON_TO_GND = true;

const uint8_t N = 4;
const uint8_t BTN_PINS[N] = {4, 5, 6, 7};
const uint8_t LED_PINS[N] = {15, 16, 17, 18};

const uint32_t DEBOUNCE_MS = 5;  // у Kailh дребезг короткий; больше — медленнее предел кликов
const char *DEFAULT_BIND = "mouse left";

USBHIDMouse Mouse;
USBHIDKeyboard Keyboard;
Preferences prefs;

enum Kind : uint8_t { NONE, MOUSE, KEY };
struct Bind {
  Kind kind;
  uint8_t mods;  // биты модификаторов HID: ctrl 1, shift 2, alt 4, cmd 8
  uint8_t code;  // кнопка мыши (MOUSE_*) или код клавиши HID (страница Keyboard, 0 — только модификаторы)
  String text;   // как назначено, в нормальном виде — для list
};
Bind binds[N];

bool pressed[N];
uint32_t changedAt[N];
bool muted[N];               // нажатие пойманное learn: до отпускания кнопка ничего не жмёт
bool learning = false;
uint32_t learnSince = 0;
const uint32_t LEARN_MS = 30000;

// ---- имена ----

struct Name {
  const char *name;
  uint8_t code;
};

const Name MOUSE_NAMES[] = {
  {"left", MOUSE_LEFT}, {"right", MOUSE_RIGHT}, {"middle", MOUSE_MIDDLE},
  {"back", MOUSE_BACKWARD}, {"forward", MOUSE_FORWARD},
};

const Name MOD_NAMES[] = {
  {"ctrl", 0x01}, {"shift", 0x02}, {"alt", 0x04}, {"opt", 0x04}, {"option", 0x04},
  {"cmd", 0x08}, {"win", 0x08}, {"gui", 0x08},
};

// Коды — физические клавиши (HID Usage Tables, Keyboard page): не зависят от раскладки компьютера.
const Name KEY_NAMES[] = {
  {"enter", 0x28}, {"esc", 0x29}, {"backspace", 0x2A}, {"tab", 0x2B}, {"space", 0x2C},
  {"minus", 0x2D}, {"equal", 0x2E}, {"lbracket", 0x2F}, {"rbracket", 0x30}, {"backslash", 0x31},
  {"semicolon", 0x33}, {"quote", 0x34}, {"grave", 0x35}, {"comma", 0x36}, {"dot", 0x37}, {"slash", 0x38},
  {"capslock", 0x39}, {"printscreen", 0x46}, {"scrolllock", 0x47}, {"pause", 0x48},
  {"insert", 0x49}, {"home", 0x4A}, {"pageup", 0x4B}, {"delete", 0x4C}, {"end", 0x4D}, {"pagedown", 0x4E},
  {"right", 0x4F}, {"left", 0x50}, {"down", 0x51}, {"up", 0x52},
};

int lookup(const Name *table, size_t n, const String &s) {
  for (size_t i = 0; i < n; i++)
    if (s == table[i].name) return table[i].code;
  return -1;
}
#define LOOKUP(t, s) lookup(t, sizeof(t) / sizeof(t[0]), s)

// a–z, 0–9, f1–f24 и имена из KEY_NAMES; -1 — нет такой клавиши
int keyCode(const String &s) {
  if (s.length() == 1) {
    char c = s[0];
    if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
    if (c >= '1' && c <= '9') return 0x1E + (c - '1');
    if (c == '0') return 0x27;
  }
  if (s[0] == 'f' && s.length() <= 3) {
    int n = s.substring(1).toInt();
    if (n >= 1 && n <= 12) return 0x3A + (n - 1);
    if (n >= 13 && n <= 24) return 0x68 + (n - 13);
  }
  return LOOKUP(KEY_NAMES, s);
}

// «mouse right», «key ctrl+c», «none» → Bind; false и err — если не разобрать
bool parseBind(String spec, Bind &b, String &err) {
  spec.trim();
  spec.toLowerCase();
  int sp = spec.indexOf(' ');
  String kind = sp < 0 ? spec : spec.substring(0, sp);
  String arg = sp < 0 ? "" : spec.substring(sp + 1);
  arg.trim();

  if (kind == "none" && arg == "") {
    b = {NONE, 0, 0, "none"};
    return true;
  }
  if (kind == "mouse") {
    int code = LOOKUP(MOUSE_NAMES, arg);
    if (code < 0) {
      err = "unknown mouse button '" + arg + "' (left right middle back forward)";
      return false;
    }
    b = {MOUSE, 0, (uint8_t)code, "mouse " + arg};
    return true;
  }
  if (kind == "key" && arg != "") {
    uint8_t mods = 0, code = 0;
    int start = 0;
    while (true) {
      int plus = arg.indexOf('+', start);
      String part = plus < 0 ? arg.substring(start) : arg.substring(start, plus);
      bool last = plus < 0;
      int m = LOOKUP(MOD_NAMES, part);
      int k = keyCode(part);
      if (last && k >= 0) {
        code = k;
      } else if (m >= 0) {
        mods |= m;
      } else {
        err = "unknown key '" + part + "' (see: keys)";
        return false;
      }
      if (last) break;
      start = plus + 1;
    }
    b = {KEY, mods, code, "key " + arg};
    return true;
  }
  err = "expected: none | mouse BUTTON | key NAME";
  return false;
}

// ---- хранение ----

String prefKey(uint8_t i) { return "b" + String(i + 1); }

void loadBinds() {
  prefs.begin("clicker", true);
  for (uint8_t i = 0; i < N; i++) {
    String err, spec = prefs.getString(prefKey(i).c_str(), DEFAULT_BIND);
    if (!parseBind(spec, binds[i], err)) parseBind(DEFAULT_BIND, binds[i], err);
  }
  prefs.end();
}

void saveBind(uint8_t i) {
  prefs.begin("clicker", false);
  prefs.putString(prefKey(i).c_str(), binds[i].text);
  prefs.end();
}

// ---- отчёты мыши и клавиатуры из того, что сейчас зажато ----

// skip — кнопка, которую не учитывать: при новом нажатии сначала отпускаем то же действие,
// чтобы каждое нажатие было новым кликом, даже если другая кнопка с тем же действием ещё зажата.
void sendState(int skip) {
  uint8_t mouse = 0;
  KeyReport kr = {};
  uint8_t nkeys = 0;
  for (uint8_t i = 0; i < N; i++) {
    if (!pressed[i] || muted[i] || i == skip) continue;
    const Bind &b = binds[i];
    if (b.kind == MOUSE) mouse |= b.code;
    if (b.kind == KEY) {
      kr.modifiers |= b.mods;
      bool dup = false;
      for (uint8_t k = 0; k < nkeys; k++) dup |= kr.keys[k] == b.code;
      if (b.code && !dup && nkeys < 6) kr.keys[nkeys++] = b.code;
    }
  }
  Mouse.buttons(mouse);
  Keyboard.sendReport(&kr);
}

void releaseAll() {
  Mouse.buttons(0);
  KeyReport kr = {};
  Keyboard.sendReport(&kr);
}

// ---- команды порта ----

String line;

void printKeys() {
  Serial.print("keys: a-z 0-9 f1-f24");
  for (auto &k : KEY_NAMES) Serial.printf(" %s", k.name);
  Serial.println();
  Serial.println("mods: ctrl shift alt(opt) cmd(win)");
  Serial.println("ok");
}

void printList() {
  for (uint8_t i = 0; i < N; i++) Serial.printf("bind %u %s\n", i + 1, binds[i].text.c_str());
  Serial.println("ok");
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd == "") return;
  learning = false;  // любая команда отменяет ожидание
  if (cmd == "learn") {
    learning = true;
    learnSince = millis();
    return;  // ответ — когда нажмут: «pressed N» и «ok», или «err timeout»
  }
  if (cmd == "list") return printList();
  if (cmd == "keys") return printKeys();
  if (cmd == "reset") {
    releaseAll();
    String err;
    for (uint8_t i = 0; i < N; i++) {
      parseBind(DEFAULT_BIND, binds[i], err);
      saveBind(i);
    }
    return printList();
  }
  if (cmd.startsWith("set ")) {
    String rest = cmd.substring(4);
    rest.trim();
    int n = rest.substring(0, 1).toInt();
    if (n < 1 || n > N || (rest.length() > 1 && rest[1] != ' ')) {
      Serial.printf("err button must be 1-%u\n", N);
      return;
    }
    Bind b;
    String err;
    if (!parseBind(rest.substring(1), b, err)) {
      Serial.println("err " + err);
      return;
    }
    releaseAll();  // не оставить зажатым старое действие кнопки
    binds[n - 1] = b;
    saveBind(n - 1);
    Serial.printf("bind %d %s\nok\n", n, b.text.c_str());
    return;
  }
  Serial.println("err commands: list | set N BIND | reset | keys");
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(line);
      line = "";
    } else if (line.length() < 80) {
      line += c;
    }
  }
}

// ---- кнопки ----

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
    muted[i] = false;
    changedAt[i] = 0;
  }
  loadBinds();
  delay(20);  // подтяжки входов успевают зарядиться
  bool diskMode = false;
  for (uint8_t i = 0; i < N; i++) {
    if (!readPressed(i)) continue;
    diskMode = true;
    pressed[i] = muted[i] = true;  // зажатая при подключении кнопка не кликает
    setLed(i, true);
  }
  if (diskMode) beginDisk();
  Mouse.begin();
  Keyboard.begin();
  USB.begin();
  Serial.println(diskMode ? "clicker: started, disk on" : "clicker: started");
}

void loop() {
  readSerial();
  uint32_t now = millis();
  if (learning && (!Serial || now - learnSince > LEARN_MS)) {  // порт закрыли или никто не нажал
    learning = false;
    if (Serial) Serial.println("err timeout");
  }
  for (uint8_t i = 0; i < N; i++) {
    bool p = readPressed(i);
    if (p == pressed[i] || now - changedAt[i] < DEBOUNCE_MS) continue;
    pressed[i] = p;
    changedAt[i] = now;
    setLed(i, p);
    if (Serial) Serial.printf("button %u %s\n", i + 1, p ? "down" : "up");
    if (p && learning) {
      learning = false;
      muted[i] = true;
      Serial.printf("pressed %u\nok\n", i + 1);
      continue;
    }
    if (!p && muted[i]) {
      muted[i] = false;
      continue;
    }
    if (p) sendState(i);  // сначала без этой кнопки — отпустить то же действие, если оно уже зажато
    sendState(-1);
  }
}
