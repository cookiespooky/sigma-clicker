#!/usr/bin/env bash
# Назначить кнопкам кликера мышь или клавиши. Назначения живут во флеше платы и переживают отключение.
#   ./bind.sh                        — мастер: нажать кнопку на кликере, затем клавишу на клавиатуре
#   ./bind.sh list                   — показать назначения
#   ./bind.sh 1 key space            — кнопка 1 = пробел
#   ./bind.sh 2 key cmd+z            — кнопка 2 = Cmd+Z (модификаторы: ctrl shift alt/opt cmd/win)
#   ./bind.sh 3 mouse right          — кнопка 3 = правая кнопка мыши (left right middle back forward)
#   ./bind.sh 4 none                 — кнопка 4 ничего не делает (лампочка горит)
#   ./bind.sh reset                  — все кнопки = левая кнопка мыши
#   ./bind.sh keys                   — имена клавиш
# Порт находит сам (/dev/cu.usbmodem*); другой — через PORT=/dev/cu.… ./bind.sh …
# Клавиши — физические: «key a» жмёт клавишу A при любой раскладке.
set -euo pipefail
export LC_ALL=C  # байты, а не символы: кириллицу разбираем сами

help() { sed -n '2,12s/^# \{0,1\}//p' "$0"; }
usage() { help; exit 1; }

case "${1:-learn}" in
  learn|list|keys|reset) mode="${1:-learn}" ;;
  [1-4]) [ $# -ge 2 ] || usage; n="$1"; shift; mode=set; cmd="set $n $*" ;;
  *) usage ;;
esac

if [ -z "${PORT:-}" ]; then
  ports=(/dev/cu.usbmodem*)
  [ -e "${ports[0]}" ] || { echo "кликер не найден: нет /dev/cu.usbmodem* — подключён ли разъём USB платы?" >&2; exit 1; }
  [ ${#ports[@]} -eq 1 ] || { echo "несколько портов: ${ports[*]} — укажите PORT=…" >&2; exit 1; }
  PORT="${ports[0]}"
fi

exec 3<>"$PORT"
stty -f "$PORT" 115200 raw -echo 2>/dev/null || true

# Отправить команду, напечатать ответ до «ok»; «err …» — в stderr и код 1.
# Строки «button N down/up» — отладка нажатий, их пропускаем.
# -n у read: bash 3.2 без него читает порт кусками и теряет всё после первой строки; с ним — по байту.
send() {
  local wait="${2:-2}" reply="" status=1
  printf '%s\n' "$1" >&3
  while IFS= read -r -t "$wait" -n 1000 -u 3 reply; do
    reply="${reply%$'\r'}"
    case "$reply" in
      ok) status=0; break ;;
      err*) echo "${reply#err }" >&2; return 1 ;;
      button*|clicker:*|"") ;;
      *) echo "$reply" ;;
    esac
  done
  [ $status -eq 0 ] || { [ -n "$reply" ] || echo "плата не ответила — прошивка с назначениями залита?" >&2; }
  return $status
}

[ "$mode" = set ] && { send "$cmd"; exit; }
[ "$mode" != learn ] && { send "$mode"; exit; }

# ---- мастер ----

# Ctrl+буква → «ctrl+буква»: коды 1–26
ctrl_name() { local c=$(( $1 + 96 )); printf "ctrl+\\$(printf %03o $c)"; }

# Латиница и символы раскладки US → имя клавиши; заглавные и верхний ряд — с shift.
ascii_name() {
  local c="$1"
  case "$c" in
    [a-z0-9]) echo "$c" ;;
    [A-Z]) echo "shift+$(printf %s "$c" | tr A-Z a-z)" ;;
    ' ') echo space ;; '-') echo minus ;; '=') echo equal ;; '[') echo lbracket ;; ']') echo rbracket ;;
    '\') echo backslash ;; ';') echo semicolon ;; "'") echo quote ;; '`') echo grave ;;
    ',') echo comma ;; '.') echo dot ;; '/') echo slash ;;
    '!') echo shift+1 ;; '@') echo shift+2 ;; '#') echo shift+3 ;; '$') echo shift+4 ;; '%') echo shift+5 ;;
    '^') echo shift+6 ;; '&') echo shift+7 ;; '*') echo shift+8 ;; '(') echo shift+9 ;; ')') echo shift+0 ;;
    '_') echo shift+minus ;; '+') echo shift+equal ;; '{') echo shift+lbracket ;; '}') echo shift+rbracket ;;
    '|') echo shift+backslash ;; ':') echo shift+semicolon ;; '"') echo shift+quote ;; '~') echo shift+grave ;;
    '<') echo shift+comma ;; '>') echo shift+dot ;; '?') echo shift+slash ;;
    *) return 1 ;;
  esac
}

# Русская раскладка (ЙЦУКЕН) → физическая клавиша; заглавные — с shift. «ё» на разных раскладках Mac
# стоит по-разному — не угадываем.
cyr_name() {
  local k
  case "$1" in
    й|Й) k=q ;; ц|Ц) k=w ;; у|У) k=e ;; к|К) k=r ;; е|Е) k=t ;; н|Н) k=y ;; г|Г) k=u ;; ш|Ш) k=i ;;
    щ|Щ) k=o ;; з|З) k=p ;; х|Х) k=lbracket ;; ъ|Ъ) k=rbracket ;;
    ф|Ф) k=a ;; ы|Ы) k=s ;; в|В) k=d ;; а|А) k=f ;; п|П) k=g ;; р|Р) k=h ;; о|О) k=j ;; л|Л) k=k ;;
    д|Д) k=l ;; ж|Ж) k=semicolon ;; э|Э) k=quote ;;
    я|Я) k=z ;; ч|Ч) k=x ;; с|С) k=c ;; м|М) k=v ;; и|И) k=b ;; т|Т) k=n ;; ь|Ь) k=m ;; б|Б) k=comma ;; ю|Ю) k=dot ;;
    *) return 1 ;;
  esac
  case "$1" in [$'\xd0'][$'\x90'-$'\xaf']) k="shift+$k" ;; esac  # А–Я: d0 90 – d0 af
  echo "$k"
}

# Последовательности стрелок и F-клавиш (после ESC [ или ESC O)
esc_name() {
  case "$1" in
    '[A'|OA) echo up ;; '[B'|OB) echo down ;; '[C'|OC) echo right ;; '[D'|OD) echo left ;;
    '[H'|OH|'[1~') echo home ;; '[F'|OF|'[4~') echo end ;;
    '[2~') echo insert ;; '[3~') echo delete ;; '[5~') echo pageup ;; '[6~') echo pagedown ;;
    OP|'[11~') echo f1 ;; OQ|'[12~') echo f2 ;; OR|'[13~') echo f3 ;; OS|'[14~') echo f4 ;;
    '[15~') echo f5 ;; '[17~') echo f6 ;; '[18~') echo f7 ;; '[19~') echo f8 ;;
    '[20~') echo f9 ;; '[21~') echo f10 ;; '[23~') echo f11 ;; '[24~') echo f12 ;;
    '[Z') echo shift+tab ;;
    *) return 1 ;;
  esac
}

readkey() { local k; IFS= read -r -s -n 1 "$@" k || return 1; printf %s "$k"; }

byte() { echo $(( ($(printf %d "'$1") + 256) % 256 )); }  # bash 3.2 даёт байты ≥ 128 отрицательными

# Щелчок в окне терминала (режим X10, ESC [ M уже прочитаны): байт кнопки и две координаты.
# Кнопка = (байт − 32) & 3: 0 левая, 1 средняя, 2 правая; +64 — колесо. За нажатием терминал шлёт
# отпускание (ещё 6 байт) — дочитываем, чтобы не попало в вопрос «Записать?».
capture_mouse() {
  local b x y i
  b=$(byte "$(readkey -t 1)") || true
  readkey -t 1 >/dev/null || true; readkey -t 1 >/dev/null || true
  for i in 1 2 3 4 5 6; do readkey -t 1 >/dev/null || break; done
  b=$(( b - 32 ))
  [ $b -ge 64 ] && { echo "?колесо"; return 1; }
  case $(( b & 3 )) in
    0) echo "mouse left" ;; 1) echo "mouse middle" ;; 2) echo "mouse right" ;;
    *) echo "?mouse"; return 1 ;;
  esac
}

# Прочитать одно нажатие с клавиатуры и назвать клавишу; код 1 — не распознана.
capture_key() {
  local c c2 seq name
  c=$(readkey)
  case "$c" in
    '') echo enter; return ;;                     # \n
    $'\r') echo enter; return ;;
    $'\t') echo tab; return ;;
    $'\x7f'|$'\x08') echo backspace; return ;;
    $'\e')
      c2=$(readkey -t 1) || { echo esc; return; }  # одиночный Esc — пауза в секунду
      if [ "$c2" = '[' ] || [ "$c2" = O ]; then
        seq="$c2"
        while :; do
          c2=$(readkey -t 1) || break
          seq="$seq$c2"
          case "$c2" in [A-Za-z~]) break ;; esac
        done
        [ "$seq" = '[M' ] && { capture_mouse; return; }
        esc_name "$seq" || { echo "?$seq"; return 1; }
        return
      fi
      name=$(ascii_name "$c2") || { echo "?alt"; return 1; }  # Option как Meta в настройках терминала
      echo "alt+$name"; return ;;
  esac
  local code; code=$(byte "$c")
  if [ "$code" -ge 1 ] && [ "$code" -le 26 ]; then ctrl_name "$code"; return; fi
  if [ "$code" -ge 192 ]; then                     # первый байт UTF-8: дочитать второй
    c2=$(readkey -t 1) || true
    cyr_name "$c$c2" || { echo "?$c$c2"; return 1; }
    return
  fi
  ascii_name "$c" || { echo "?$c"; return 1; }
}

old_tty=$(stty -g)
mouse_on() { printf '\033[?1000h'; }   # терминал сообщает о щелчках, а не выделяет текст
mouse_off() { printf '\033[?1000l'; }
trap 'mouse_off; stty "$old_tty"; exec 3<&-' EXIT

help
echo
echo "Сейчас:"
send list
echo
echo "Нажмите кнопку на кликере, которую назначить (30 с; Ctrl+C — выход)."
out=$(send learn 32) || exit 1
btn="${out#pressed }"
echo "Кнопка $btn. Теперь нажмите клавишу или сочетание на клавиатуре Mac — или щёлкните мышью в этом окне"
echo "(левая, правая, средняя)."
echo "  Cmd-сочетания, боковые кнопки мыши и «ничего» терминал не ловит: ./bind.sh $btn key cmd+z | mouse back | none"

stty -icanon -echo -isig -icrnl -ixon      # чтобы Ctrl+C, Ctrl+S и Enter пришли как клавиши
mouse_on
if ! key=$(capture_key); then
  mouse_off; stty "$old_tty"
  echo "Не узнал нажатие (${key#\?}). Назначьте по имени: ./bind.sh $btn key ИМЯ — имена: ./bind.sh keys" >&2
  exit 1
fi
mouse_off; stty "$old_tty"

case "$key" in mouse\ *) spec="$key" ;; *) spec="key $key" ;; esac
printf 'Кнопка %s → %s. Записать? [Y/n] ' "$btn" "$spec"
IFS= read -r ans || ans=n
case "$ans" in ''|y|Y|д|Д) ;; *) echo "Не записано."; exit 1 ;; esac
send "set $btn $spec"
