// Флешка «CLICKER» с одним файлом bind.htm — страницей назначений (bind.html, вшитой при сборке в bind_html.h).
// Диск только для чтения, собирается на лету: FAT12, сектор 512 байт, кластер = сектор.
//   сектор 0 — загрузочный, 1 — таблица FAT, 2 — корневой каталог (16 записей), с 3 — содержимое файла.
// Появляется, только если при подключении зажата кнопка кликера (см. setup в click_buttons.ino).
#pragma once
#include "USBMSC.h"
#include "bind_html.h"  // const char BIND_HTML[] — генерирует flash.sh из bind.html

const uint16_t DISK_SECTOR = 512;
const uint32_t DISK_SECTORS = 128;  // 64 КБ: место под страницу до 62,5 КБ
const uint32_t DATA_START = 3;
const uint32_t HTML_SIZE = sizeof(BIND_HTML) - 1;
static_assert(HTML_SIZE <= (DISK_SECTORS - DATA_START) * DISK_SECTOR, "bind.html не влезает в диск — увеличить DISK_SECTORS");

USBMSC MSC;
uint8_t diskBoot[DISK_SECTOR], diskFat[DISK_SECTOR], diskRoot[DISK_SECTOR];

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static void fat12Set(uint32_t n, uint16_t v) {
  uint32_t off = n * 3 / 2;
  if (n & 1) {
    diskFat[off] = (diskFat[off] & 0x0F) | ((v << 4) & 0xF0);
    diskFat[off + 1] = v >> 4;
  } else {
    diskFat[off] = v;
    diskFat[off + 1] = (diskFat[off + 1] & 0xF0) | ((v >> 8) & 0x0F);
  }
}

static void buildDisk() {
  uint8_t *b = diskBoot;
  b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
  memcpy(b + 3, "MSDOS5.0", 8);
  put16(b + 11, DISK_SECTOR);
  b[13] = 1;                     // секторов в кластере
  put16(b + 14, 1);              // зарезервировано: только загрузочный
  b[16] = 1;                     // одна таблица FAT
  put16(b + 17, 16);             // записей в корне
  put16(b + 19, DISK_SECTORS);
  b[21] = 0xF8;                  // несъёмный носитель
  put16(b + 22, 1);              // секторов на FAT
  put16(b + 24, 1);
  put16(b + 26, 1);
  b[36] = 0x80;
  b[38] = 0x29;
  put32(b + 39, 0xC11C4E25);     // серийный номер тома
  memcpy(b + 43, "CLICKER    ", 11);
  memcpy(b + 54, "FAT12   ", 8);
  b[510] = 0x55; b[511] = 0xAA;

  // кластеры файла 2…2+n−1 подряд
  uint32_t n = (HTML_SIZE + DISK_SECTOR - 1) / DISK_SECTOR;
  fat12Set(0, 0xFF8);
  fat12Set(1, 0xFFF);
  for (uint32_t c = 2; c < 2 + n; c++) fat12Set(c, c + 1 < 2 + n ? c + 1 : 0xFFF);

  uint8_t *e = diskRoot;
  memcpy(e, "CLICKER    ", 11);
  e[11] = 0x08;                  // метка тома
  e += 32;
  memcpy(e, "BIND    HTM", 11);
  e[11] = 0x01;                  // только чтение
  e[12] = 0x18;                  // имя и расширение показывать строчными: bind.htm
  uint16_t date = ((2026 - 1980) << 9) | (11 << 5) | 18;
  put16(e + 16, date);           // создан
  put16(e + 18, date);           // открыт
  put16(e + 24, date);           // изменён
  put16(e + 26, 2);              // первый кластер
  put32(e + 28, HTML_SIZE);
}

static void readSector(uint32_t lba, uint32_t off, uint8_t *dst, uint32_t len) {
  const uint8_t *src = nullptr;
  if (lba == 0) src = diskBoot;
  else if (lba == 1) src = diskFat;
  else if (lba == 2) src = diskRoot;
  if (src) { memcpy(dst, src + off, len); return; }
  memset(dst, 0, len);
  if (lba < DATA_START) return;
  uint32_t pos = (lba - DATA_START) * DISK_SECTOR + off;
  if (pos < HTML_SIZE) memcpy(dst, BIND_HTML + pos, min(len, HTML_SIZE - pos));
}

static int32_t diskRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  uint8_t *dst = (uint8_t *)buffer;
  uint32_t done = 0;
  while (done < bufsize) {  // запрос может идти через несколько секторов
    uint32_t abs = offset + done;
    uint32_t sec = lba + abs / DISK_SECTOR, off = abs % DISK_SECTOR;
    uint32_t len = min(bufsize - done, (uint32_t)DISK_SECTOR - off);
    readSector(sec, off, dst + done, len);
    done += len;
  }
  return bufsize;
}

static int32_t diskWrite(uint32_t, uint32_t, uint8_t *, uint32_t) { return -1; }
static bool diskStartStop(uint8_t, bool, bool) { return true; }

void beginDisk() {
  buildDisk();
  MSC.vendorID("Clicker");
  MSC.productID("Bind page");
  MSC.productRevision("1.0");
  MSC.onRead(diskRead);
  MSC.onWrite(diskWrite);
  MSC.onStartStop(diskStartStop);
  MSC.isWritable(false);
  MSC.mediaPresent(true);
  MSC.begin(DISK_SECTORS, DISK_SECTOR);
}
