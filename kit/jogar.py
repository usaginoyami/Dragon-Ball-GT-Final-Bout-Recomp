#!/usr/bin/env python3
"""Final Bout recomp - lancador de 1 clique.
Pega o SEU disco (.cue/.bin), extrai os arquivos, recompila o jogo, compila e abre.
Nada do jogo e distribuido junto: tudo e gerado na sua maquina a partir do seu disco.
Uso: Jogar.bat [disco.cue|disco.bin] [--reconstruir] [opcoes do jogo...]"""
import os, sys, json, hashlib, subprocess, shutil, time, glob
from concurrent.futures import ThreadPoolExecutor

AQUI = os.path.dirname(os.path.abspath(__file__))
FONTE = os.path.join(AQUI, 'fonte')
FERR = os.path.join(AQUI, 'ferramentas')
DADOS = os.path.join(AQUI, 'dados')
JOGO = os.path.join(DADOS, 'jogo')
WIN = os.name == 'nt'
EXE = os.path.join(DADOS, 'recomp_test.exe')
sys.path.insert(0, AQUI)
import iso

NECESSARIOS = ['SLUS_004.93', 'STEP00.BIN', 'STEP01.BIN', 'STEP02.BIN', 'STEP03.BIN', 'STEP40.BIN', 'STEP999.BIN']

def die(msg):
    print('\nERRO: ' + msg); sys.exit(1)

def escolher_disco(args):
    cfg = os.path.join(DADOS, 'disco.txt')
    for a in args:
        if not a.startswith('--') and os.path.exists(a):
            return os.path.abspath(a)
    if os.path.exists(cfg):
        p = open(cfg, encoding='utf-8').read().strip()
        if os.path.exists(p):
            return p
    p = ''
    try:
        import tkinter; from tkinter import filedialog
        r = tkinter.Tk(); r.withdraw(); r.attributes('-topmost', True)
        p = filedialog.askopenfilename(title='Escolha o disco do Dragon Ball GT - Final Bout (.cue ou .bin)',
                                       filetypes=[('Imagem de disco', '*.cue *.bin *.iso'), ('Todos', '*.*')])
        r.destroy()
    except Exception:
        pass
    if not p:
        p = input('Caminho do seu disco (.cue ou .bin): ').strip().strip('"')
    if not p or not os.path.exists(p):
        die('disco nao encontrado.')
    return os.path.abspath(p)

def extrair(disco):
    print('[1/3] Lendo o disco...')
    bin_path, d = iso.abrir_imagem(disco)
    achados = {}
    for nome, ext, tam in d.arquivos():
        base = nome.split('/')[-1]
        if base in NECESSARIOS and base not in achados:
            achados[base] = (ext, tam)
    falta = [n for n in NECESSARIOS if n not in achados]
    if falta:
        die('este disco nao parece ser o Dragon Ball GT - Final Bout (NTSC-U, SLUS-004.93). Faltam: ' + ', '.join(falta))
    os.makedirs(JOGO, exist_ok=True)
    for n, (ext, tam) in achados.items():
        open(os.path.join(JOGO, n), 'wb').write(d.ler(ext, tam))
    return bin_path

def chave():
    h = hashlib.sha256()
    for n in NECESSARIOS:
        h.update(open(os.path.join(JOGO, n), 'rb').read())
    for p in sorted(glob.glob(os.path.join(FONTE, '*'))):
        if os.path.isfile(p):
            h.update(os.path.basename(p).encode()); h.update(open(p, 'rb').read())
    return h.hexdigest()

def zig_cmd():
    z = os.environ.get('FB_ZIG')
    if z:
        return [z]
    exe = os.path.join(FERR, 'zig', 'zig.exe' if WIN else 'zig')
    if os.path.exists(exe):
        return [exe]
    die('compilador (ferramentas/zig) nao encontrado. Baixe o pacote completo.')

def compilar():
    gen = os.path.join(DADOS, 'gen'); obj = os.path.join(DADOS, 'obj')
    shutil.rmtree(gen, ignore_errors=True); shutil.rmtree(obj, ignore_errors=True)
    os.makedirs(obj, exist_ok=True)
    print('[2/3] Recompilando o jogo (so na primeira vez ou apos atualizar; leva alguns minutos)...')
    py = [sys.executable]
    r = subprocess.run(py + [os.path.join(FONTE, 'build.py'), os.path.join(JOGO, 'SLUS_004.93'), JOGO, os.path.join(FONTE, 'overlays.txt'), gen],
                       cwd=DADOS, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-2000:], r.stderr[-2000:]); die('falha ao gerar o codigo do jogo (build.py).')
    sdl = os.environ.get('FB_SDL') or os.path.join(FERR, 'SDL2', 'x86_64-w64-mingw32')
    zig = zig_cmd()
    env = dict(os.environ); env['ZIG_GLOBAL_CACHE_DIR'] = os.path.join(DADOS, 'zigcache'); env['ZIG_LOCAL_CACHE_DIR'] = os.path.join(DADOS, 'zigcache')
    fontes = sorted(glob.glob(os.path.join(gen, '*.c'))) + [os.path.join(FONTE, f) for f in
        ('runtime.c', 'hw.c', 'gpu.c', 'gte.c', 'spu.c', 'mdec.c', 'xa.c', 'video.c')]
    total = len(fontes); feito = [0]
    def comp(f):
        o = os.path.join(obj, os.path.basename(f) + '.o')
        c = zig + ['cc', '-target', 'x86_64-windows-gnu', '-O1', '-w', '-I', FONTE, '-DUSE_SDL',
                   '-I', os.path.join(sdl, 'include'), '-I', os.path.join(sdl, 'include', 'SDL2'), '-c', f, '-o', o]
        r = subprocess.run(c, env=env, capture_output=True, text=True)
        feito[0] += 1
        print('   %d/%d  %s' % (feito[0], total, os.path.basename(f)))
        if r.returncode:
            print(r.stderr[-1500:]); raise RuntimeError('falha compilando ' + f)
        return o
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=max(2, os.cpu_count() or 2)) as ex:
        objs = list(ex.map(comp, fontes))
    print('   ligando...')
    c = zig + ['cc', '-target', 'x86_64-windows-gnu'] + objs + [os.path.join(sdl, 'lib', 'libSDL2.dll.a'),
         os.path.join(sdl, 'lib', 'libSDL2main.a'), '-lm', '-lwinmm', '-o', EXE]
    r = subprocess.run(c, env=env, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-2000:]); die('falha ao ligar o executavel.')
    dll = os.path.join(sdl, 'bin', 'SDL2.dll')
    if os.path.exists(dll):
        shutil.copy(dll, DADOS)
    shutil.rmtree(gen, ignore_errors=True); shutil.rmtree(obj, ignore_errors=True)
    try: os.remove(EXE[:-4] + '.pdb')
    except OSError: pass
    print('   pronto em %.0f s.' % (time.time() - t0))

def main():
    args = sys.argv[1:]
    reconstruir = '--reconstruir' in args
    resto = [a for a in args if a != '--reconstruir']
    os.makedirs(DADOS, exist_ok=True)
    disco = escolher_disco(resto)
    open(os.path.join(DADOS, 'disco.txt'), 'w', encoding='utf-8').write(disco)
    bin_path = extrair(disco)
    k = chave(); kf = os.path.join(DADOS, 'build.key')
    if reconstruir or not os.path.exists(EXE) or not os.path.exists(kf) or open(kf).read() != k:
        compilar()
        open(kf, 'w').write(k)
    else:
        print('[2/3] Jogo ja compilado.')
    if os.environ.get('FB_SO_COMPILAR'):
        print('Compilado (modo teste).'); return
    print('[3/3] Abrindo o jogo...  (F11 tela cheia, F3 FPS, ESC sai)')
    extra = [a for a in resto if a.startswith('--') or (not os.path.exists(a))]
    cmd = [EXE, 'jogo/SLUS_004.93', '--bin', bin_path, '--janela', '--cd-turbo', '10'] + extra
    sys.exit(subprocess.call(cmd, cwd=DADOS))

if __name__ == '__main__':
    try:
        main()
    except SystemExit:
        raise
    except Exception as e:
        print('\nERRO inesperado: %r' % (e,)); sys.exit(1)
