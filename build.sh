#!/bin/sh
# uso: ./build.sh SLUS_004.93 PASTA_COM_STEPxx.BIN   (precisa de python3 e gcc)
set -e
python3 build.py "$1" "$2" overlays.txt gen
SDLF=""; SDLL=""
if command -v sdl2-config >/dev/null 2>&1; then SDLF="-DUSE_SDL $(sdl2-config --cflags)"; SDLL="$(sdl2-config --libs)"; fi
for f in gen/*.c runtime.c hw.c gpu.c gte.c spu.c mdec.c xa.c video.c; do gcc -O1 -w -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -I. $SDLF -c "$f" -o "${f%.c}.o"; done
case "$(uname -s)" in MINGW*|MSYS*) SDLL="$SDLL -lwinmm";; esac
gcc gen/*.o runtime.o hw.o gpu.o gte.o spu.o mdec.o xa.o video.o $SDLL -lm -o recomp_test
echo "pronto: ./recomp_test $1 --bin SEU_DISCO.bin   (opcoes: -cd, -cdd, -v)"
