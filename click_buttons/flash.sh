#!/usr/bin/env bash
# Собрать и залить кликер через разъём «USB» (родной USB S3; COM не нужен).
# Без аргумента только собирает; с портом — ещё и заливает: ./flash.sh /dev/cu.usbmodem14101
# Порт: `ls /dev/cu.usbmodem*`. Если плата не отвечает: подключить с зажатой BOOT, залить, нажать RST.
# CDCOnBoot=cdc: плата видна и мышью, и портом с отладкой («button 1 down»). После RST Mac видит плату через несколько секунд.
set -euo pipefail
cd "$(dirname "$0")"
FQBN="esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,FlashSize=16M,PSRAM=disabled"
# Страница назначений вшивается в прошивку — флешка CLICKER (disk.h). bind_html.h — сборочный, не в git.
{ printf 'const char BIND_HTML[] = R"=HTML=('; cat bind.html; printf ')=HTML=";\n'; } > bind_html.h
arduino-cli compile --fqbn "$FQBN" .
if [ $# -gt 0 ]; then arduino-cli upload --fqbn "$FQBN" -p "$1" .; fi
