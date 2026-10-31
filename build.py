#!/usr/bin/env python3
"""Gera o C do jogo: python3 build.py SLUS_004.93 PASTA_COM_STEP overlays.txt SAIDA
overlays.txt: uma linha por overlay: NOME BASE_HEX [RAIZES_HEX...]   (ex.: STEP00 8003dfa0 8003dfd0)"""
import subprocess, sys, os
exe, stepdir, ovtxt, outdir = sys.argv[1:5]
os.makedirs(outdir, exist_ok=True)
here = os.path.dirname(os.path.abspath(__file__))
def run(args): subprocess.run([sys.executable, os.path.join(here, 'recomp.py')] + args, check=True)
def fnv(b):
    h = 0xcbf29ce484222325
    for x in b: h = ((h ^ x) * 0x100000001b3) & 0xffffffffffffffff
    return h
ents = []
ext = set()
for i, line in enumerate(l.split() for l in open(ovtxt) if l.strip() and not l.startswith('#')):
    name, base, roots = line[0], line[1], line[2:]
    path = os.path.join(stepdir, name + '.BIN')
    run([path, os.path.join(outdir, 'ov%d.c' % i), '--raw', '0x' + base, '--prefix', 'ov%d_' % i] + ['0x' + r for r in roots])
    ext |= set(int(x, 16) for x in open(os.path.join(outdir, 'ov%d.c.ext' % i)).read().split())
    data = open(path, 'rb').read(); size = len(data) & ~3
    ents.append((i, name, int(base, 16), size, fnv(data[:size])))
extra = ['0x%x' % a for a in sorted(ext) if 0x80010000 <= a < 0x8002bd00]
run([exe, os.path.join(outdir, 'game.c'), '0x80010c54', '0x8001bfd0'] + extra)
print('raizes extras vindas dos overlays:', len(extra))
out = ['#include "runtime.h"', '#include "overlay.h"']
for i, *_ in ents: out.append('extern const FuncEntry ov%d_func_table[]; extern const int ov%d_func_table_len;' % (i, i))
out.append('Overlay overlays[] = {')
for i, name, base, size, h in ents: out.append('  { "%s", 0x%x, %d, 0x%xULL, ov%d_func_table, 0 },' % (name, base, size, h, i))
out.append('};\nconst int overlays_n = %d;' % len(ents))
out.append('const int overlay_counts[] = {' + ', '.join('0' for _ in ents) + '};')
# comprimentos reais das tabelas
out.append('void overlay_init(void) {')
for i, *_ in ents: out.append('  overlays[%d].n = ov%d_func_table_len;' % (i, i))
out.append('}')
open(os.path.join(outdir, 'overlays.c'), 'w').write('\n'.join(out))
print('overlays:', len(ents))
