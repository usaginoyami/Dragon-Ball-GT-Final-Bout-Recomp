/* Hardware do PS1 em C: registradores de I/O, CD-ROM, DMA, interrupcoes. */
#include "runtime.h"
#include "overlay.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <setjmp.h>

uint8_t* ram_ptr(uint32_t a);
double now_s(void);
extern int hw_verbose;

static uint8_t iomem[0x2000];
static uint32_t istat, imask, dicr_flags;
uint32_t irq_handler_struct;           /* jmp_buf de SetCustomExitFromException */
static int in_isr, irq_en = 1;

static uint32_t io_raw(uint32_t a, int n) { uint32_t v = 0; memcpy(&v, iomem + (a - 0x1f801000), n); return v; }
static void io_store(uint32_t a, int n, uint32_t v) { memcpy(iomem + (a - 0x1f801000), &v, n); }

/* ---------------- interrupcoes ---------------- */
static jmp_buf isr_jb;
void isr_exit(void) { if (in_isr) longjmp(isr_jb, 1); }   /* ReturnFromException: nunca volta ao chamador */
void irq_check(void) {
    if (in_isr || !irq_en || !irq_handler_struct || !(istat & imask)) return;
    uint32_t jb = irq_handler_struct, fn = rd32(jb);
    if (!fn) return;
    uint32_t sv[32], shi = HI, slo = LO; memcpy(sv, R, sizeof sv);
    in_isr = 1;
    for (int guard = 0; guard < 8 && (istat & imask); guard++) {
        R[29] = rd32(jb + 4); R[30] = rd32(jb + 8);                       /* sp, fp */
        for (int i = 0; i < 8; i++) R[16 + i] = rd32(jb + 12 + 4 * i);    /* s0-s7 */
        R[28] = rd32(jb + 44); R[31] = 0; R[4] = 0; R[2] = 1;   /* retorno de setjmp != 0 */                       /* gp */
        if (hw_verbose > 1) fprintf(stderr, "[irq] trapIntr istat=%03x imask=%03x\n", istat, imask);
        if (setjmp(isr_jb) == 0) call_addr(fn);
    }
    in_isr = 0; memcpy(R, sv, sizeof sv); HI = shi; LO = slo;
}
uint32_t hw_critical(int enter) {   /* syscall 1 = EnterCriticalSection, 2 = Exit */
    uint32_t old = irq_en; irq_en = enter ? 0 : 1; if (!enter) irq_check(); return old;
}
static void raise_irq(int bit) { istat |= 1u << bit; irq_check(); }

/* ---------------- CD-ROM ---------------- */
typedef struct { double t; int intr; uint8_t resp[16]; int n; int sector; } Ev;
#define MAXEV 64
static Ev evq[MAXEV]; static int evn;
static struct {
    uint8_t idx, ien, ifl, par[16], resp[16], mode, stat; int npar, nresp, rpos, busy;
    uint8_t sec[2352]; int sec_off, sec_len, sec_pos, drq;
    int filt_file, filt_ch; int setloc, setloc_pending; int loc, reading; FILE* bin; int rd_fail; int data_taken; double last_sector_t;
} cd;
void hw_set_bin(const char* p) { cd.bin = fopen(p, "rb"); if (!cd.bin) fprintf(stderr, "[cd] nao consegui abrir %s\n", p); cd.stat = 0x02; }

static double cd_speed = 1.0;
void hw_set_speed(double s) { if (s >= 1) cd_speed = s; }
static void ev_bubble(void) { for (int i = evn - 1; i > 0 && evq[i - 1].t > evq[i].t; i--) { Ev tmp = evq[i - 1]; evq[i - 1] = evq[i]; evq[i] = tmp; } }
static void ev_push(double dt, int intr, const uint8_t* r, int n, int sector) {
    if (evn >= MAXEV) return;
    Ev* e = &evq[evn++]; e->t = now_s() + dt / cd_speed; e->intr = intr; e->n = n; e->sector = sector;
    if (n) memcpy(e->resp, r, n);
}
static void ev_push1(double dt, int intr, uint8_t b) { ev_push(dt, intr, &b, 1, 0); }
static int bcd(int v) { return ((v / 10) << 4) | (v % 10); }
static int unbcd(int v) { return (v >> 4) * 10 + (v & 15); }
static double sector_period(void) { return ((cd.mode & 0x80) ? 1.0 / 150 : 1.0 / 75) / cd_speed; }

static void cd_command(uint8_t c) {
    uint8_t* p = cd.par; uint8_t st = cd.stat | (cd.reading ? 0x20 : 0);
    uint8_t r[8]; cd.busy = 1;
    if (hw_verbose) fprintf(stderr, "[cd] comando %02x param=%02x %02x %02x (n=%d)\n", c, p[0], p[1], p[2], cd.npar);
    switch (c) {
    case 0x01: ev_push1(0.0005, 3, st); break;                                           /* Getstat */
    case 0x02: if (hw_verbose > 1) fprintf(stderr, "[cd] Setloc %02x:%02x:%02x\n", p[0], p[1], p[2]); cd.setloc = (unbcd(p[0]) * 60 + unbcd(p[1])) * 75 + unbcd(p[2]); cd.setloc_pending = 1; ev_push1(0.0005, 3, st); break;  /* Setloc */
    case 0x06: case 0x1b:                                                                 /* ReadN / ReadS */
        for (int i = 0; i < evn; ) { if (evq[i].sector) { memmove(&evq[i], &evq[i + 1], (--evn - i) * sizeof(Ev)); } else i++; }   /* nao duplica a cadeia de leitura */
        if (cd.setloc_pending) { cd.loc = cd.setloc; cd.setloc_pending = 0; }
        cd.reading = 1; ev_push1(0.0005, 3, cd.stat | 0x20);
        ev_push1(0.05, 1, cd.stat | 0x20); evq[evn - 1].sector = 1; break;
    case 0x09: {                                                                          /* Pause */
        ev_push1(0.0005, 3, st); cd.reading = 0; ev_push1(0.07, 2, cd.stat); break; }
    case 0x08: ev_push1(0.0005, 3, st); cd.reading = 0; ev_push1(0.03, 2, cd.stat); break; /* Stop */
    case 0x07: case 0x0a: ev_push1(0.0005, 3, st); ev_push1(0.03, 2, cd.stat); break;     /* MotorOn / Init */
    case 0x0d: cd.filt_file = p[0]; cd.filt_ch = p[1]; ev_push1(0.0005, 3, st); break;
    case 0x0b: case 0x0c: ev_push1(0.0005, 3, st); break;                      /* Mute Demute Setfilter */
    case 0x0e: cd.mode = p[0]; ev_push1(0.0005, 3, st); break;                            /* Setmode */
    case 0x10: case 0x11: {                                                               /* GetlocL / GetlocP */
        int l = cd.loc; int mm = l / 4500, ss = (l / 75) % 60, ff = l % 75;
        if (c == 0x10) { r[0] = bcd(mm); r[1] = bcd(ss); r[2] = bcd(ff); r[3] = 2; r[4] = r[5] = r[6] = r[7] = 0; }
        else { r[0] = 1; r[1] = 1; r[2] = bcd(mm); r[3] = bcd(ss); r[4] = bcd(ff); r[5] = bcd(mm); r[6] = bcd(ss); r[7] = bcd(ff); }
        ev_push(0.0005, 3, r, 8, 0); break; }
    case 0x13: r[0] = st; r[1] = 1; r[2] = 1; ev_push(0.0005, 3, r, 3, 0); break;         /* GetTN: faixas 01..01 */
    case 0x14: r[0] = st; r[1] = 0; r[2] = 2; ev_push(0.0005, 3, r, 3, 0); break;         /* GetTD */
    case 0x15: case 0x16: if (cd.setloc_pending) { cd.loc = cd.setloc; cd.setloc_pending = 0; } cd.reading = 0; ev_push1(0.0005, 3, st); ev_push1(0.05, 2, cd.stat); break;     /* SeekL / SeekP */
    case 0x19: if (p[0] == 0x20) { r[0] = 0x94; r[1] = 0x09; r[2] = 0x19; r[3] = 0xc0; ev_push(0.0005, 3, r, 4, 0); } else ev_push1(0.0005, 3, st); break;
    case 0x1a: ev_push1(0.0005, 3, st);                                                   /* GetID: disco licenciado NTSC-U */
        { uint8_t id[8] = {0x02, 0x00, 0x20, 0x00, 'S', 'C', 'E', 'A'}; ev_push(0.03, 2, id, 8, 0); } break;
    case 0x1e: ev_push1(0.0005, 3, st); ev_push1(0.05, 2, cd.stat); break;               /* ReadTOC */
    default: if (hw_verbose) fprintf(stderr, "[cd] comando %02x nao suportado\n", c);
        r[0] = st | 1; r[1] = 0x40; ev_push(0.0005, 5, r, 2, 0); break;
    }
    cd.npar = 0;
}
static void cd_load_sector(int lba) {
    long idx = (long)lba - 150; cd.sec_len = 0; memset(cd.sec, 0, sizeof cd.sec);
    if (!cd.bin || idx < 0) return;
    if (fseek(cd.bin, idx * 2352L, SEEK_SET) == 0) { size_t n = fread(cd.sec, 1, 2352, cd.bin); (void)n; }
}
static void cd_poll(void) {
    double now = now_s();
    while (evn > 0 && cd.ifl == 0 && evq[0].t <= now) {
        if (evq[0].sector && in_isr) break;   /* so entrega o proximo setor depois que a ISR atual terminou (como no hardware, 13 ms depois) */
        if (evq[0].sector && !cd.data_taken && now - cd.last_sector_t < 0.25) break;   /* espera o jogo pegar o setor anterior */
        Ev e = evq[0]; memmove(evq, evq + 1, (--evn) * sizeof(Ev));
        if (e.sector) {                                   /* setores de audio XA vao para o decodificador, nao para o jogo */
            cd_load_sector(cd.loc);
            int sub = cd.sec[18];
            if ((cd.mode & 0x40) && (sub & 0x04) && ((sub & 0x20) || 1) && (!(cd.mode & 0x08) || (cd.sec[16] == cd.filt_file && cd.sec[17] == cd.filt_ch))) {
                if (hw_verbose > 1) fprintf(stderr, "[cd] setor %d XA (sub=%02x)\n", cd.loc, sub);
                xa_feed(cd.sec); cd.loc++; cd.data_taken = 1; cd.last_sector_t = now;
                if (cd.reading) { ev_push(0, 1, (uint8_t[]){cd.stat | 0x20}, 1, 1); evq[evn - 1].t = e.t + sector_period(); }
                continue;
            }
            if ((cd.mode & 0x40) && (sub & 0x04) && (cd.mode & 0x08)) {   /* audio de outro canal: descartado */
                if (hw_verbose > 1) fprintf(stderr, "[cd] setor %d descartado (filtro)\n", cd.loc);
                cd.loc++; cd.data_taken = 1; cd.last_sector_t = now;
                if (cd.reading) { ev_push(0, 1, (uint8_t[]){cd.stat | 0x20}, 1, 1); evq[evn - 1].t = e.t + sector_period(); }
                continue;
            }
        }
        cd.busy = 0;
        memcpy(cd.resp, e.resp, e.n); cd.nresp = e.n; cd.rpos = 0; cd.ifl = e.intr;
        if (e.sector) {
            cd.data_taken = 0; cd.last_sector_t = now; cd.drq = 0; if (hw_verbose > 1) fprintf(stderr, "[cd] setor %d entregue\n", cd.loc);
            cd.loc++;
            if (cd.reading) { ev_push(0, 1, (uint8_t[]){cd.stat | 0x20}, 1, 1); evq[evn - 1].t = e.t + sector_period(); }
        }
        if (hw_verbose) fprintf(stderr, "[cd] INT%d entregue (ien=%02x imask=%03x irq_en=%d in_isr=%d)\n", e.intr, cd.ien, imask, irq_en, in_isr);
        if (cd.ifl & cd.ien) { raise_irq(2); }
    }
}
static uint32_t cd_read_reg(int r) {
    cd_poll();
    switch (r) {
    case 0: return cd.idx | ((cd.npar == 0) << 3) | ((cd.npar < 16) << 4) | ((cd.nresp > cd.rpos) << 5) | ((cd.drq && cd.sec_pos < cd.sec_len) << 6) | (cd.busy << 7);
    case 1: return cd.rpos < cd.nresp ? cd.resp[cd.rpos++] : 0;
    case 2: { if (cd.drq && cd.sec_pos < cd.sec_len) { uint8_t b = cd.sec[cd.sec_off + cd.sec_pos++]; if (cd.sec_pos >= cd.sec_len) cd.data_taken = 1; return b; } return 0; }
    default: return (cd.idx & 1) ? (0xe0 | cd.ifl) : (0xe0 | cd.ien);
    }
}
static void cd_write_reg(int r, uint8_t v) {
    cd_poll();
    if (r == 0) { cd.idx = v & 3; return; }
    switch (cd.idx * 4 + r) {
    case 1: cd_command(v); break;                                    /* idx0 r1: comando */
    case 2: if (cd.npar < 16) cd.par[cd.npar++] = v; break;          /* idx0 r2: parametro */
    case 3:                                                          /* idx0 r3: pedido de dados */
        if (v & 0x80) { cd.drq = 1; cd.sec_pos = 0;
            if (cd.mode & 0x20) { cd.sec_off = 12; cd.sec_len = 2340; } else { cd.sec_off = 24; cd.sec_len = 2048; } }
        else cd.drq = 0; break;
    case 6: cd.ien = v & 0x1f; break;                                /* idx1 r2: mascara de interrupcao */
    case 7: cd.ifl &= ~(v & 0x1f); if (v & 0x40) cd.npar = 0;        /* idx1 r3: reconhece interrupcao */
        if (!cd.ifl) { cd.nresp = 0; cd.rpos = 0; cd_poll(); } break;
    default: break;
    }
}

/* ---------------- DMA ---------------- */
static void dma_done(int ch) {
    uint32_t dicr = io_raw(0x1f8010f4, 4);
    if (hw_verbose && ch == 3) fprintf(stderr, "[dma-irq] ch3 dicr=%08x imask=%03x\n", dicr, imask);
    if (dicr & (1u << (16 + ch))) { dicr_flags |= 1u << (24 + ch); if (dicr & (1u << 23)) raise_irq(3); }
}
static void dma_run(int ch) {
    uint32_t base = 0x1f801080 + ch * 0x10, madr = io_raw(base, 4) & 0x1ffffc, bcr = io_raw(base + 4, 4), chcr = io_raw(base + 8, 4);
    uint32_t sync = (chcr >> 9) & 3;
    uint32_t words = sync == 0 ? (bcr & 0xffff ? bcr & 0xffff : 0x10000) : (bcr & 0xffff) * (bcr >> 16);
    if (hw_verbose && ch == 3) fprintf(stderr, "[dma3] CD -> RAM %08x, %u bytes\n", madr | 0x80000000, words * 4);
    if (ch == 3 && madr < 0x60000 && madr + words * 4 > 0x3df00) overlay_invalidate();   /* so se o DMA cai na area de overlays */
    if (ch == 0 || ch == 1) { if (ch == 0) mdec_dma_in(madr, words); else mdec_dma_out(madr, words); io_store(base + 8, 4, chcr & ~((1u << 24) | (1u << 28))); dma_done(ch); return; }
    if (ch == 4) { spu_dma(madr, words, chcr & 1); io_store(base + 8, 4, chcr & ~((1u << 24) | (1u << 28))); dma_done(4); return; }
    if (ch == 2) { gpu_dma(madr, bcr, chcr); io_store(base + 8, 4, chcr & ~((1u << 24) | (1u << 28))); dma_done(2); return; }
    if (ch == 3) {                                      /* CD -> RAM */
        uint8_t* d = ram_ptr(madr);
        for (uint32_t i = 0; i < words * 4 && d; i++) d[i] = (cd.drq && cd.sec_pos < cd.sec_len) ? cd.sec[cd.sec_off + cd.sec_pos++] : 0;
        if (cd.sec_pos >= cd.sec_len) cd.data_taken = 1;
        if (getenv("TRACE_HDR") && words == 8) { extern int g_trace_n; g_trace_n = atoi(getenv("TRACE_HDR")); }
        if (hw_verbose && d && words == 8) { fprintf(stderr, "[dma3] cab32:"); for (int i = 0; i < 32; i++) fprintf(stderr, " %02x", d[i]); fprintf(stderr, "\n"); }
        if (hw_verbose && d) fprintf(stderr, "[dma3] primeiros bytes: %02x %02x %02x %02x %02x %02x %02x %02x (drq=%d)\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], cd.drq);
    } else if (ch == 6) {                               /* tabela de ordenacao reversa (ClearOTagR) */
        for (uint32_t i = 0; i < words; i++) {
            uint32_t a = (madr - 4 * i) & 0x1ffffc; uint32_t v = (i == words - 1) ? 0x00ffffff : ((a - 4) & 0x1fffff) | 0;
            wr32(a, v);
        }
    }
    io_store(base + 8, 4, chcr & ~((1u << 24) | (1u << 28)));
    dma_done(ch);
}

/* ---------------- barramento de I/O ---------------- */
uint32_t spu_read(uint32_t,int); void spu_write(uint32_t,int,uint32_t); void spu_dma(uint32_t,uint32_t,int);
void mdec_cmd_write(uint32_t); void mdec_ctl_write(uint32_t); uint32_t mdec_status(void); void mdec_dma_in(uint32_t,uint32_t); void mdec_dma_out(uint32_t,uint32_t);
void xa_feed(const uint8_t*); void xa_reset(void);
void gpu_gp0(uint32_t); void gpu_gp1(uint32_t); uint32_t gpu_stat(void), gpu_read(void); void gpu_dma(uint32_t,uint32_t,uint32_t);
uint32_t io_rd(uint32_t a, int n) {
    if (a < 0x1f801000 || a >= 0x1f803000) { static int w; if (w++ < 10) fprintf(stderr, "[io] leitura fora de mapa %08x\n", a); return 0; }
    if (a >= 0x1f801800 && a <= 0x1f801803) return cd_read_reg(a - 0x1f801800);
    if (a == 0x1f801070) return istat;
    if (a == 0x1f801074) return imask;
    if (a == 0x1f8010f4) { uint32_t d = io_raw(a, 4) & 0x00ffffff; d |= dicr_flags; if ((d & (1u << 15)) || ((d & (1u << 23)) && ((d >> 16) & 0x7f & (dicr_flags >> 24)))) d |= 1u << 31; return d; }
    if (a == 0x1f801814) return gpu_stat();
    if (a == 0x1f801820) return 0;
    if (a == 0x1f801824) return mdec_status();
    if (a == 0x1f801810) return gpu_read();
    if (a >= 0x1f801c00 && a < 0x1f801e00) return spu_read(a, n);
    if (a >= 0x1f801100 && a < 0x1f801130 && (a & 0xf) == 0)   /* contadores de raiz: tempo real */
        return (uint32_t)(now_s() * 33868800.0 / ((a == 0x1f801120) ? 8.0 : 1.0)) & 0xffff;
    return io_raw(a, n);
}
void io_wr(uint32_t a, int n, uint32_t v) {
    if (a < 0x1f801000 || a >= 0x1f803000) { static int w; if (w++ < 10) fprintf(stderr, "[io] escrita fora de mapa %08x\n", a); return; }
    if (a >= 0x1f801800 && a <= 0x1f801803) { cd_write_reg(a - 0x1f801800, (uint8_t)v); return; }
    if (a == 0x1f801070) { istat &= v; return; }                /* escrever 0 reconhece */
    if (getenv("DBGIO") && (a == 0x1f801074 || (a >= 0x1f8010f0 && a < 0x1f8010f8))) { extern uint32_t dbg_lastfn(void), dbg_ra(void); fprintf(stderr, "[io] w%d %08x = %08x  (fn=%08x ra=%08x)\n", n, a, v, dbg_lastfn(), dbg_ra()); if (a == 0x1f8010f4 && v == 0) { extern void dbg_ring(int); dbg_ring(24); } }
    if (a == 0x1f801074) { imask = v & 0x7ff; irq_check(); return; }
    if (a >= 0x1f801c00 && a < 0x1f801e00) { spu_write(a, n, v); return; }
    if (a == 0x1f801820) { mdec_cmd_write(v); return; }
    if (a == 0x1f801824) { mdec_ctl_write(v); return; }
    if (a == 0x1f801810) { gpu_gp0(v); return; }
    if (a == 0x1f801814) { gpu_gp1(v); return; }
    if (a == 0x1f8010f4) { uint32_t old = io_raw(a, 4); dicr_flags &= ~(v & 0x7f000000); io_store(a, 4, (v & 0x00ff803f) | (old & 0)); return; }
    if (a >= 0x1f801080 && a < 0x1f8010f0 && (a & 0xf) == 8) {   /* CHCR dos canais DMA */
        int ch = (a - 0x1f801080) / 0x10; io_store(a, 4, v);
        if (v & (1u << 24)) dma_run(ch);
        return;
    }
    io_store(a, n, v);
}
void hw_poll(void) { cd_poll(); irq_check(); }
