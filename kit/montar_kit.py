#!/usr/bin/env python3
"""Monta o pacote de 1 clique (zip) a partir do repositorio + ferramentas baixadas.
Uso: python3 montar_kit.py SAIDA_DIR PY_WIN.tar.gz ZIGWIN.whl SDL2-devel-mingw.tar.gz
 - PY_WIN: python-build-standalone (windows msvc install_only .tar.gz)
 - ZIGWIN: wheel ziglang para win_amd64 (pip download ziglang --platform win_amd64 --only-binary=:all: --no-deps)
 - SDL2-devel: SDL2-devel-2.30.x-mingw.tar.gz (libsdl.org / GitHub releases)
So ferramentas e codigo do projeto entram no pacote; nada do jogo."""
import os, sys, shutil, tarfile, zipfile, glob
saida, pytgz, zigwhl, sdltgz = sys.argv[1:5]
aqui = os.path.dirname(os.path.abspath(__file__)); repo = os.path.dirname(aqui)
shutil.rmtree(saida, ignore_errors=True); os.makedirs(saida)
fonte = os.path.join(saida, 'fonte'); os.makedirs(fonte)
for pat in ('*.c', '*.h', '*.py', 'overlays.txt'):
    for f in glob.glob(os.path.join(repo, pat)):
        shutil.copy(f, fonte)
for f in ('jogar.py', 'iso.py', 'Jogar.bat', 'LEIA-ME.txt'):
    shutil.copy(os.path.join(aqui, f), saida)
ferr = os.path.join(saida, 'ferramentas'); os.makedirs(ferr)
# python
tmp = os.path.join(saida, '_tmp'); os.makedirs(tmp)
tarfile.open(pytgz).extractall(tmp)
py = os.path.join(ferr, 'python'); shutil.move(os.path.join(tmp, 'python'), py)
for pat in ('**/*.pdb', '**/__pycache__', 'Lib/test', 'Lib/idlelib', 'Lib/ensurepip', 'Lib/site-packages', 'Lib/turtledemo', 'tcl/tcl8/*/*.tm', 'include', 'libs', 'Scripts'):
    for p in glob.glob(os.path.join(py, pat), recursive=True):
        shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else os.remove(p)
# zig (somente o necessario para x86_64-windows-gnu)
zipfile.ZipFile(zigwhl).extractall(tmp)
zg = os.path.join(ferr, 'zig'); shutil.move(os.path.join(tmp, 'ziglang'), zg)
lib = os.path.join(zg, 'lib')
for d in ('libcxx', 'libcxxabi', 'libtsan', 'libunwind', 'libfuzzer', 'docs', 'build-web', 'init'):
    shutil.rmtree(os.path.join(lib, d), ignore_errors=True)
for d in ('darwin', 'freebsd', 'glibc', 'musl', 'netbsd', 'openbsd', 'wasi'):
    shutil.rmtree(os.path.join(lib, 'libc', d), ignore_errors=True)
inc = os.path.join(lib, 'libc', 'include')
for d in os.listdir(inc):
    if d != 'any-windows-any':
        shutil.rmtree(os.path.join(inc, d), ignore_errors=True)
# SDL2
tarfile.open(sdltgz).extractall(tmp)
sd = glob.glob(os.path.join(tmp, 'SDL2-*', 'x86_64-w64-mingw32'))[0]
dst = os.path.join(ferr, 'SDL2', 'x86_64-w64-mingw32'); os.makedirs(os.path.dirname(dst))
shutil.copytree(sd, dst, ignore=shutil.ignore_patterns('share', 'sdl2-config*', 'cmake', 'pkgconfig', 'libSDL2*.a' if False else ''))
shutil.rmtree(tmp, ignore_errors=True)
print('kit montado em', saida)
