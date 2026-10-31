#include "runtime.h"
#include "overlay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
int hw_verbose; void hw_poll(void); void hw_set_bin(const char*); uint32_t hw_critical(int); extern uint32_t irq_handler_struct; void irq_check(void);
double now_s(void);

uint32_t R[32], HI, LO;
static uint8_t ram[0x200000], spad[0x400];
static uint32_t cop0[32];
static uint64_t g_calls; static uint64_t g_limit = 4000000000ull;
static int g_verbose; static uint32_t ring[64], ring_ra[64]; static int ring_i;
static uint32_t heap_addr, heap_size;

static uint8_t* ptr(uint32_t a);
uint8_t* ram_ptr(uint32_t a) { return ptr(a); }
static uint8_t* ptr(uint32_t a) {
    a &= 0x1fffffff;
    if (a < 0x200000) return ram + a;
    if (a >= 0x1f800000 && a < 0x1f800400) return spad + (a - 0x1f800000);
    return NULL;
}
uint32_t io_rd(uint32_t a, int n); void io_wr(uint32_t a, int n, uint32_t v);
uint8_t rd8(uint32_t a) { uint8_t* p = ptr(a); return p ? *p : (uint8_t)io_rd(a, 1); }
uint16_t rd16(uint32_t a) { uint8_t* p = ptr(a); if (!p) return (uint16_t)io_rd(a, 2); uint16_t v; memcpy(&v, p, 2); return v; }
uint32_t rd32(uint32_t a) { uint8_t* p = ptr(a); if (!p) return io_rd(a, 4); uint32_t v; memcpy(&v, p, 4); return v; }
void wr8(uint32_t a, uint32_t v) { uint8_t* p = ptr(a); if (p) *p = (uint8_t)v; else io_wr(a, 1, v); }
void wr16(uint32_t a, uint32_t v) { uint8_t* p = ptr(a); if (p) { uint16_t x = (uint16_t)v; memcpy(p, &x, 2); } else io_wr(a, 2, v); }
void wr32(uint32_t a, uint32_t v) { uint8_t* p = ptr(a); if (p) memcpy(p, &v, 4); else io_wr(a, 4, v); }
uint32_t lwl(uint32_t a, uint32_t old) { uint32_t b = a & 3, w = rd32(a & ~3u); uint32_t m[4] = {0x00ffffff, 0x0000ffff, 0x000000ff, 0}; int s[4] = {24, 16, 8, 0}; return (old & m[b]) | (w << s[b]); }
uint32_t lwr(uint32_t a, uint32_t old) { uint32_t b = a & 3, w = rd32(a & ~3u); uint32_t m[4] = {0, 0xff000000, 0xffff0000, 0xffffff00}; int s[4] = {0, 8, 16, 24}; return (old & m[b]) | (w >> s[b]); }
void swl(uint32_t a, uint32_t v) { uint32_t b = a & 3, w = rd32(a & ~3u); uint32_t m[4] = {0xffffff00, 0xffff0000, 0xff000000, 0}; int s[4] = {24, 16, 8, 0}; wr32(a & ~3u, (w & m[b]) | (v >> s[b])); }
void swr(uint32_t a, uint32_t v) { uint32_t b = a & 3, w = rd32(a & ~3u); uint32_t m[4] = {0, 0x000000ff, 0x0000ffff, 0x00ffffff}; int s[4] = {0, 8, 16, 24}; wr32(a & ~3u, (w & m[b]) | (v << s[b])); }

uint32_t dbg_lastfn(void) { return ring[(ring_i - 1) & 63]; }
uint32_t dbg_ra(void) { return R[31]; }
void dbg_ring(int n) { fprintf(stderr, "   ring:"); for (int i = n; i >= 1; i--) fprintf(stderr, " %08x(%08x)", ring[(ring_i - i) & 63], ring_ra[(ring_i - i) & 63]); fprintf(stderr, "\n"); }
static void dump_ring(void) {
    fprintf(stderr, "ultimas funcoes (mais recente por ultimo):\n");
    for (int i = 0; i < 64; i++) { uint32_t a = ring[(ring_i + i) & 63]; if (a) fprintf(stderr, "  %08x (ra=%08x)\n", a, ring_ra[(ring_i + i) & 63]); }
}
static void on_alarm(int s) { (void)s; fprintf(stderr, "\n[travou] loop sem fim (provavelmente espera de hardware). Chamadas: %llu\n", (unsigned long long)g_calls); dump_ring(); _exit(3); }
static void run_vblank(void);
static uint32_t g_watch, g_wval; static int g_watch_init;
int g_trace_n;
void trace_enter(uint32_t a) {
    if (g_trace_n > 0) { g_trace_n--; fprintf(stderr, "-> %08x (ra=%08x a0=%08x a1=%08x)\n", a, R[31], R[4], R[5]); }
    if (!g_watch_init) { g_watch_init = 1; if (getenv("WATCH")) g_watch = strtoul(getenv("WATCH"), 0, 16); }
    if (g_watch) { uint32_t v = rd32(g_watch); if (v != g_wval) { fprintf(stderr, "[watch] %08x: %x -> %x em %08x (ra=%08x) chamada %llu\n", g_watch, g_wval, v, a, R[31], (unsigned long long)g_calls); g_wval = v; } }
    ring_ra[ring_i & 63] = R[31]; ring[ring_i++ & 63] = a; g_calls++;
    if (g_verbose) fprintf(stderr, "-> %08x (ra=%08x a0=%08x a1=%08x)\n", a, R[31], R[4], R[5]);
    if (!(g_calls & 0xff)) { run_vblank(); hw_poll(); }
    if (g_calls > g_limit) { fprintf(stderr, "[limite] %llu chamadas\n", (unsigned long long)g_calls); dump_ring(); exit(4); }
}
void cpu_syscall(uint32_t pc) {
    if (R[4] == 1) { R[2] = hw_critical(1); return; }      /* EnterCriticalSection */
    if (R[4] == 2) { hw_critical(0); return; }             /* ExitCriticalSection */
    fprintf(stderr, "[syscall] em %08x a0=%u\n", pc, R[4]);
}
void cpu_break(uint32_t pc) { fprintf(stderr, "[break] em %08x\n", pc); dump_ring(); exit(5); }
void cpu_unknown(uint32_t pc, uint32_t w) { fprintf(stderr, "[instrucao desconhecida] %08x em %08x\n", w, pc); dump_ring(); exit(6); }
void cpu_fell_off(uint32_t pc) { fprintf(stderr, "[fim de funcao] executou %08x, fora do codigo traduzido\n", pc); dump_ring(); exit(7); }
void cpu_div(uint32_t a, uint32_t b) { int32_t x = a, y = b; if (!y) { LO = x < 0 ? 1 : 0xffffffff; HI = x; } else if (x == INT32_MIN && y == -1) { LO = 0x80000000; HI = 0; } else { LO = x / y; HI = x % y; } }
void cpu_divu(uint32_t a, uint32_t b) { if (!b) { LO = 0xffffffff; HI = a; } else { LO = a / b; HI = a % b; } }
uint32_t cop0_rd(int r) { return cop0[r]; }
void cop0_wr(int r, uint32_t v) { cop0[r] = v; }

/* ---- ganchos HLE: substituem funcoes da biblioteca Sony por versoes em C ---- */
static uint32_t g_vcount;
double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double g_t0; static uint32_t vblank_count(void) { if (!g_t0) g_t0 = now_s(); return (uint32_t)((now_s() - g_t0) * 59.94); }
static uint32_t g_vcb; static int in_cb; static uint32_t last_disp;
void hw_set_speed(double); void spu_mix(int16_t*, int); int audio_queued(void); void audio_push(const int16_t*, int);
static double audio_t = 0; static void audio_run(void) {
    double now = now_s(); if (audio_t == 0) audio_t = now;
    int n = (int)((now - audio_t) * 44100); if (n <= 0) return; if (n > 4410) { n = 4410; audio_t = now; } else audio_t += n / 44100.0;
    static int16_t buf[4410 * 2]; spu_mix(buf, n);
    { static FILE* af; static int init; if (!init) { init = 1; if (getenv("AUDIO_RAW")) af = fopen(getenv("AUDIO_RAW"), "wb"); } if (af) fwrite(buf, 4, n, af); }
    int q = audio_queued(); if (q >= 0 && q > 6000) return;   /* fila cheia: descarta */
    audio_push(buf, n);
} extern int gpu_logn; void gpu_vblank(void); int gpu_dump(const char*); int gpu_dump_vram(const char*); void gpu_info(void);
uint32_t g_frames; static uint32_t g_shots[32], g_nshots, g_maxf = 0xffffffff;
static uint32_t force_state=0xffffffff, force_at=0;
static uint32_t pad_buf[2]; static uint32_t pad_mask, pad_start_at = 0xffffffff, pad_period = 90;
void pad_set(uint32_t m) { pad_mask = m; }
static int g_win;
int video_init(void); void video_present(void);
static struct { uint32_t m, f; } presses[64]; static int npress;
static void add_press(const char* s) {
    static const char* nm[] = { "SELECT","L3","R3","START","UP","RIGHT","DOWN","LEFT","L2","R2","L1","R1","TRIANGLE","CIRCLE","CROSS","SQUARE" };
    char b[32]; strncpy(b, s, 31); b[31] = 0; char* at = strchr(b, '@'); if (!at || npress >= 64) return; *at = 0;
    for (int i = 0; i < 16; i++) if (!strcmp(b, nm[i])) { presses[npress].m = 1u << i; presses[npress++].f = atoi(at + 1); }
}
static void pad_update(void) {
    uint32_t m = pad_mask;
    for (int i = 0; i < npress; i++) if (g_frames >= presses[i].f && g_frames < presses[i].f + 4) m |= presses[i].m;
    if (g_frames >= pad_start_at && (g_frames - pad_start_at) % pad_period < 4) m |= 0x0008;     /* Start automatico (teste) */
    for (int p = 0; p < 2; p++) if (pad_buf[p]) {
        uint32_t a = pad_buf[p];
        wr8(a, 0x00); wr8(a + 1, 0x41);
        wr8(a + 2, ~m & 0xff); wr8(a + 3, (~m >> 8) & 0xff);
        if (p == 1) { wr8(a, 0xff); }             /* porta 2: sem controle */
    }
}
static void run_vblank(void) {
    if (!g_vcb || in_cb) return;
    uint32_t c = vblank_count(); if (c == last_disp) return; last_disp = c; g_frames++; gpu_vblank(); audio_run(); if (g_win) video_present(); pad_update();
    if (getenv("GPU_LOGAT") && g_frames == (uint32_t)atoi(getenv("GPU_LOGAT"))) gpu_logn = 3000;
    if (getenv("VERBOSEAT")) { int f = atoi(getenv("VERBOSEAT")); if ((int)g_frames == f) g_verbose = 1; if ((int)g_frames == f + 1) g_verbose = 0; }
    if (getenv("DUMPAT") && (int)g_frames == atoi(getenv("DUMPAT"))) { FILE* df = fopen("ram.bin", "wb"); fwrite(ram, 1, sizeof ram, df); fclose(df); }
    if (force_state != 0xffffffff && g_frames == force_at) { wr16(0x80034fdc, force_state); wr16(0x80056878, 1); fprintf(stderr, "[estado] forcado %x no quadro %u\n", force_state, g_frames); }
    { static uint32_t last_state = 0xffffffff; uint32_t st = (rd32(0x80034fdc) & 0xffff) | (getenv("DBGSUB") ? rd32(0x80057484) << 16 : 0); if (st != last_state) { if (getenv("DBGSTATE")) fprintf(stderr, "[estado] quadro %u: %02x -> %02x (sub %x)\n", g_frames, last_state & 0xff, st, rd32(0x80057484)); last_state = st; } }
    if (getenv("TRACE_SUB")) { static int done; extern int g_trace_n; if (!done && rd32(0x80057484) == (uint32_t)strtoul(getenv("TRACE_SUB"), 0, 16) && g_frames % 60 == 0) { done = 1; g_trace_n = 20000; } }
    if (getenv("DBGS") && g_frames % 120 == 0) fprintf(stderr, "[estado] quadro %u substado=%x main=%x\n", g_frames, rd32(0x80057484), rd32(R[28] + 0x108));
    for (uint32_t k = 0; k < g_nshots; k++) if (g_frames == g_shots[k]) { char fn[64]; sprintf(fn, "shot_%u.ppm", g_frames); gpu_dump(fn); if (k == g_nshots - 1) gpu_dump_vram("vram.ppm"); gpu_info(); }
    if (g_frames >= g_maxf) { fprintf(stderr, "[frames] fim\n"); exit(0); }
    uint32_t sv[32], shi = HI, slo = LO; memcpy(sv, R, sizeof sv);
    in_cb = 1; R[29] -= 0x400; R[31] = 0; call_addr(g_vcb); in_cb = 0;
    memcpy(R, sv, sizeof sv); HI = shi; LO = slo;
}
static void wait_vblank(void) { uint32_t c = vblank_count(); while (vblank_count() == c) usleep(500); run_vblank(); }
static int hle_log;
static int ov_active = -1; static int g_skipvid = 1;
int hle_hook(uint32_t a) {
    switch (a) {
    case 0x8003e14c: /* STEP00: init do video LOGO.STR -> pula (finge que terminou) */
        if (g_skipvid && ov_active == 0) { wr32(0x8003F944, 0); fprintf(stderr, "[hle] pulando video de abertura\n"); return 1; }
        return 0;
    case 0x8003f24c: /* STEP01: substado de video STR -> finge que terminou e avanca */
        if (g_skipvid && ov_active == 1) { uint32_t s = rd32(0x80057484); fprintf(stderr, "[hle] pulando video STR (substado %x -> %x)\n", s, s + 1); wr32(0x800562a4, 0); wr32(0x80057484, s + 1); return 1; }
        return 0;
    case 0x800163b8: /* tentativa de pegar quadro do STR (o laco tem limite de 2000 tentativas, pensado p/ CPU lenta) */
        usleep(150); hw_poll(); return 0;
    case 0x8001f580: if (getenv("DBGST")) fprintf(stderr, "[st] StSetStream(mode=%u, ini=%u, fim=%u) ra=%08x\n", R[4], R[5], R[6], R[31]); return 0;
    case 0x8001bbec: /* VSync(mode) */
        if ((int32_t)R[4] >= 0) wait_vblank();    /* 0 = espera proximo vblank, n = espera n */
        g_vcount = vblank_count(); R[2] = g_vcount;
        if (hle_log++ < 6) fprintf(stderr, "[hle] VSync(%d) -> %u\n", (int)R[4], g_vcount); return 1;
    case 0x8001be60: /* VSyncCallback(fn): guarda o callback, chamado a cada vblank */
        g_vcb = R[4]; R[2] = 0; fprintf(stderr, "[hle] VSyncCallback(%08x)\n", g_vcb); return 1;
    default: return 0;
    }
}

/* ---- BIOS em alto nivel ---- */
static uint32_t next_event = 0xf1000000;

/* ---- eventos do kernel e memory card (HLE) ---- */
typedef struct { uint32_t cls, spec; int used, delivered; } KEv;
static KEv kev[64];
static uint8_t mc[131072]; static int mc_init, mc_new = 1;
static void mc_load(void) {
    if (mc_init) return; mc_init = 1;
    FILE* f = fopen("memcard1.mcd", "rb"); if (f) { size_t n = fread(mc, 1, sizeof mc, f); fclose(f); if (n == sizeof mc) return; }
    memset(mc, 0, sizeof mc);
    mc[0] = 'M'; mc[1] = 'C'; mc[127] = 'M' ^ 'C';                                   /* cabecalho formatado */
    for (int i = 1; i < 16; i++) { uint8_t* e = mc + i * 128; e[0] = 0xa0; e[8] = e[9] = 0xff; e[127] = 0xa0; }
    for (int i = 16; i < 36; i++) { uint8_t* e = mc + i * 128; memset(e, 0xff, 4); e[8] = e[9] = 0xff; }
    memcpy(mc + 63 * 128, mc, 128);
}
static void mc_save(void) { FILE* f = fopen("memcard1.mcd", "wb"); if (f) { fwrite(mc, 1, sizeof mc, f); fclose(f); } }
static void kev_deliver(uint32_t cls, uint32_t spec) { for (int i = 0; i < 64; i++) if (kev[i].used && kev[i].cls == cls && kev[i].spec == spec) kev[i].delivered = 1; }
static int card_class(uint32_t c) { return c == 0xf4000001u || c == 0xf0000011u; }
static void bios_call(uint32_t tbl, uint32_t fn);
static void init_kernel_tables(void) {
    for (uint32_t i = 0; i < 0xc0; i++) wr32(0x200 + i * 4, 0xbfc00000u | (0xa0u << 8) | i);
    for (uint32_t i = 0; i < 0x60; i++) wr32(0x874 + i * 4, 0xbfc00000u | (0xb0u << 8) | i);
    for (uint32_t i = 0; i < 0x20; i++) wr32(0x674 + i * 4, 0xbfc00000u | (0xc0u << 8) | i);
}
static void bios_call(uint32_t tbl, uint32_t fn) {
    uint32_t a0 = R[4], a1 = R[5];
    const char* name = "?";
    R[2] = 0;
    if (tbl == 0xa0) switch (fn) {
        case 0x39: name = "InitHeap"; heap_addr = a0; heap_size = a1; break;
        case 0x9f: name = "SetMem"; break;
        case 0x13: name = "setjmp"; wr32(a0, R[31]); wr32(a0 + 4, R[29]); wr32(a0 + 8, R[30]);
            for (int i = 0; i < 8; i++) wr32(a0 + 12 + 4 * i, R[16 + i]);
            wr32(a0 + 44, R[28]); R[2] = 0; break;
        case 0xab: { name = "_card_info"; mc_load(); int slot = (a0 & 0x10) ? 1 : 0;
            if (slot) kev_deliver(0xf4000001u, 0x0100); else if (mc_new) kev_deliver(0xf4000001u, 0x2000); else kev_deliver(0xf4000001u, 0x0004); R[2] = 1; break; }
        case 0xac: { name = "_card_load"; mc_load(); if (a0 & 0x10) kev_deliver(0xf4000001u, 0x0100); else { mc_new = 0; kev_deliver(0xf4000001u, 0x0004); } R[2] = 1; break; }
        case 0x44: name = "FlushCache"; break;
        case 0x45: name = "init_a0_table"; break;
        case 0x57: name = "GetA0Table?"; R[2] = 0x200; break;
        case 0x3f: name = "printf"; break;
        default: break;
    } else if (tbl == 0xb0) switch (fn) {
        case 0x08: { name = "OpenEvent"; if (a0 == 0xf4000001u || a0 == 0xf0000011u) fprintf(stderr, "[card] OpenEvent cls=%08x spec=%x modo=%x func=%08x\n", a0, a1, R[6], R[7]); R[2] = next_event++; int i = R[2] & 63; kev[i].cls = a0; kev[i].spec = a1; kev[i].used = 1; kev[i].delivered = 0; break; }
        case 0x07: name = "DeliverEvent"; kev_deliver(a0, a1); break;
        case 0x20: { name = "UnDeliverEvent"; for (int i = 0; i < 64; i++) if (kev[i].used && kev[i].cls == a0 && kev[i].spec == a1) kev[i].delivered = 0; break; }
        case 0x4e: { name = "_card_write"; mc_load(); uint32_t s = a1 & 0x3ff; if (!(a0 & 0x10) && s < 1024) { for (int k = 0; k < 128; k++) mc[s * 128 + k] = rd8(R[6] + k); mc_save(); kev_deliver(0xf4000001u, 0x0004); R[2] = 1; } else kev_deliver(0xf4000001u, 0x0100); break; }
        case 0x4f: { name = "_card_read"; mc_load(); uint32_t s = a1 & 0x3ff; if (!(a0 & 0x10) && s < 1024) { for (int k = 0; k < 128; k++) wr8(R[6] + k, mc[s * 128 + k]); kev_deliver(0xf4000001u, 0x0004); R[2] = 1; } else kev_deliver(0xf4000001u, 0x0100); break; }
        case 0x0c: name = "EnableEvent"; R[2] = 1; break;
        case 0x17: name = "ReturnFromException"; { extern void isr_exit(void); isr_exit(); } break;
        case 0x19: name = "SetCustomExitFromException"; irq_handler_struct = a0; break;
        case 0x0d: name = "DisableEvent"; R[2] = 1; break;
        case 0x0a: { name = "WaitEvent"; KEv* e = &kev[a0 & 63]; if (e->used && card_class(e->cls)) e->delivered = 0; R[2] = 1; break; }
        case 0x0b: { name = "TestEvent"; KEv* e = &kev[a0 & 63]; if (e->used && card_class(e->cls)) { R[2] = e->delivered; e->delivered = 0; } else R[2] = 1; break; }
        case 0x12: name = "InitPad"; pad_buf[0] = R[4]; pad_buf[1] = R[6]; R[2] = 1; break;
        case 0x13: name = "StartPad"; break;
        case 0x56: name = "GetC0Table"; R[2] = 0x674; break;
        case 0x57: name = "GetB0Table"; R[2] = 0x874; break;
        case 0x4a: name = "InitCARD"; break;
        case 0x4b: name = "StartCARD"; break;
        default: break;
    } else if (tbl == 0xc0) switch (fn) {
        case 0x0a: name = "ChangeClearRCnt"; break;
        case 0x02: name = "SysEnqIntRP"; break;
        default: break;
    }
    fprintf(stderr, "[bios] %02x:%02x %s(%08x, %08x)\n", tbl & 0xff, fn, name, a0, a1);
}

static int ov_valid;
void overlay_invalidate(void) { ov_valid = 0; }
static uint64_t fnv_ram(uint32_t base, uint32_t size) {
    uint64_t h = 0xcbf29ce484222325ULL; uint8_t* p = ptr(base);
    for (uint32_t i = 0; i < size; i++) h = (h ^ p[i]) * 0x100000001b3ULL;
    return h;
}
static void (*overlay_find(uint32_t k))(void) {
    if (!(ov_valid && ov_active >= 0)) {
        ov_active = -1;
        for (int i = 0; i < overlays_n; i++) {
            if (k < overlays[i].base || k >= overlays[i].base + overlays[i].size) continue;
            if (fnv_ram(overlays[i].base, overlays[i].size) == overlays[i].hash) { ov_active = i; ov_valid = 1; fprintf(stderr, "[overlay] %s ativo\n", overlays[i].name); break; }
        }
        if (ov_active < 0) return NULL;
    }
    const Overlay* o = &overlays[ov_active];
    int lo = 0, hi = o->n - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (o->tab[m].addr == k) return o->tab[m].fn; if (o->tab[m].addr < k) lo = m + 1; else hi = m - 1; }
    return NULL;
}
void call_addr(uint32_t a) {
    if (a == 0xa0 || a == 0xb0 || a == 0xc0) { bios_call(a, R[9]); return; }
    if ((a & 0xffff0000u) == 0xbfc00000u) { bios_call((a >> 8) & 0xff, a & 0xff); return; }
    uint32_t k = a; if (k < 0x200000) k |= 0x80000000;
    int lo = 0, hi = func_table_len - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (func_table[m].addr == k) { func_table[m].fn(); return; }
        if (func_table[m].addr < k) lo = m + 1; else hi = m - 1;
    }
    if (k >= 0x8003df00) { void (*f)(void) = overlay_find(k); if (f) { f(); return; } }
    if (getenv("DUMP_OV")) { FILE* d = fopen(getenv("DUMP_OV"), "wb"); fwrite(ptr(0x8003dfa0), 1, 0x2000, d); fclose(d); }
    fprintf(stderr, "[nao traduzido] chamada para %08x ra=%08x t1=%08x (overlay ou ponteiro de BIOS?)\n", a, R[31], R[9]); dump_ring(); exit(8);
}

extern void func_entry_dummy(void);
int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s SLUS_004.93 [-v]\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { perror("exe"); return 1; }
    uint8_t h[0x800]; if (fread(h, 1, 0x800, f) != 0x800) return 1;
    uint32_t pc, gp, addr, size; memcpy(&pc, h + 0x10, 4); memcpy(&gp, h + 0x14, 4); memcpy(&addr, h + 0x18, 4); memcpy(&size, h + 0x1c, 4);
    if (fread(ram + (addr & 0x1fffffff), 1, size, f) != size) return 1; fclose(f);
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else if (!strcmp(argv[i], "-cd")) hw_verbose = 1;
        else if (!strcmp(argv[i], "-cdd")) hw_verbose = 2;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) { if (g_nshots < 32) g_shots[g_nshots++] = atoi(argv[++i]); else ++i; }
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) add_press(argv[++i]);
        else if (!strcmp(argv[i], "--cd-turbo") && i + 1 < argc) hw_set_speed(atof(argv[++i]));
        else if (!strcmp(argv[i], "--pular-video")) g_skipvid = 1;
        else if (!strcmp(argv[i], "--video")) g_skipvid = 0;
        else if (!strcmp(argv[i], "--janela")) g_win = 1;
        else if (!strcmp(argv[i], "--estado") && i + 1 < argc) { char *a = argv[++i]; force_state = strtoul(a, &a, 16); if (*a == '@') force_at = atoi(a + 1); }
        else if (!strcmp(argv[i], "--start-at") && i + 1 < argc) pad_start_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) g_maxf = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bin") && i + 1 < argc) hw_set_bin(argv[++i]);
    }
    overlay_init(); init_kernel_tables();
    R[28] = gp; R[29] = 0x801fff00; R[31] = 0;
    if (g_win) { if (!video_init()) { fprintf(stderr, "janela indisponivel (compile com SDL2: veja build.sh)\n"); return 1; } putenv("ALARM=0"); }
    signal(SIGALRM, on_alarm); alarm(getenv("ALARM") ? atoi(getenv("ALARM")) : 30);
    fprintf(stderr, "[inicio] entry=%08x\n", pc);
    call_addr(pc);
    fprintf(stderr, "[fim] entry retornou. chamadas=%llu\n", (unsigned long long)g_calls);
    return 0;
}
