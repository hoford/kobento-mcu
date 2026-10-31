#!/bin/bash
# Build an STM32CubeU5 example from its STM32CubeIDE project files, without CubeIDE.
#   tools/cube_build.sh <example dir>   e.g. .../Examples/DSI/DSI_VideoMode_SingleBuffer
# Reads the sources from .project and the defines and includes from .cproject.
# Output: build/<example>.elf, .bin and .map in this repo.
set -euo pipefail
EX=$(cd "$1" && pwd); IDE=$EX/STM32CubeIDE; NAME=$(basename "$EX")
ROOT=$(cd "$(dirname "$0")/.." && pwd); OUT=$ROOT/build/$NAME
GCC=${ARM_GCC:-$HOME/tools/arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi/bin}/arm-none-eabi-
mkdir -p "$OUT"
resolve() {  # PARENT-n-PROJECT_LOC/x -> path
  local u=$1 n rest
  n=$(sed -E 's/^PARENT-([0-9]+)-PROJECT_LOC.*/\1/' <<<"$u"); rest=${u#PARENT-*-PROJECT_LOC/}
  local d=$IDE; for ((i=0;i<n;i++)); do d=$(dirname "$d"); done; echo "$d/$rest"
}
SRCS=()
while read -r u; do SRCS+=("$(resolve "$u")"); done < <(grep -oE '<locationURI>[^<]+\.(c|s)</locationURI>' "$IDE/.project" | sed -E 's/<\/?locationURI>//g')
for f in "$IDE"/Application/*/*.c "$IDE"/Application/*/*.s; do [ -f "$f" ] && SRCS+=("$f"); done
FLAGS=(-mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard -O2 -g -ffunction-sections -fdata-sections -Wall)
while read -r v; do
  case $v in */*) FLAGS+=("-I$(cd "$IDE" && cd "${v#../}" && pwd)");; *) FLAGS+=("-D$v");; esac
done < <(grep -oE 'listOptionValue builtIn="false" value="[^"]+"' "$IDE/.cproject" | sed -E 's/.*value="([^"]+)"/\1/' | sort -u)
OBJS=()
for s in "${SRCS[@]}"; do
  o=$OUT/$(basename "${s%.*}").o; OBJS+=("$o")
  "${GCC}gcc" "${FLAGS[@]}" -x $([[ $s == *.s ]] && echo assembler-with-cpp || echo c) -c "$s" -o "$o"
done
"${GCC}gcc" "${FLAGS[@]}" -T"$(ls "$IDE"/*FLASH.ld)" --specs=nano.specs --specs=nosys.specs \
  -Wl,--gc-sections -Wl,-Map="$OUT/$NAME.map" -Wl,--no-warn-rwx-segments "${OBJS[@]}" -o "$OUT/$NAME.elf" 2>&1 | grep -v "is not implemented" || true
"${GCC}objcopy" -O binary "$OUT/$NAME.elf" "$OUT/$NAME.bin"
"${GCC}size" "$OUT/$NAME.elf"
echo "$OUT/$NAME.bin"
