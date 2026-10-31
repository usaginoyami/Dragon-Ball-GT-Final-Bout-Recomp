/* janela SDL2 + teclado (compilar com -DUSE_SDL) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef USE_SDL
#include <SDL.h>
void gpu_render(uint32_t*, int*, int*);
void pad_set(uint32_t);
static SDL_Window* win; static SDL_Renderer* ren; static SDL_Texture* tex; static int tw, th;
static uint32_t fb[640 * 480];
static uint32_t keys, padbits; static SDL_AudioDeviceID adev; static SDL_GameController* gc;
static void gc_open(void) { if (gc) return; for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { gc = SDL_GameControllerOpen(i); if (gc) { fprintf(stderr, "[pad] controle: %s\n", SDL_GameControllerName(gc)); break; } } }
static uint32_t gc_bits(void) {
    if (!gc) return 0; uint32_t m = 0;
#define B(btn, bit) if (SDL_GameControllerGetButton(gc, btn)) m |= 1u << (bit)
    B(SDL_CONTROLLER_BUTTON_BACK, 0); B(SDL_CONTROLLER_BUTTON_LEFTSTICK, 1); B(SDL_CONTROLLER_BUTTON_RIGHTSTICK, 2); B(SDL_CONTROLLER_BUTTON_START, 3);
    B(SDL_CONTROLLER_BUTTON_DPAD_UP, 4); B(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 5); B(SDL_CONTROLLER_BUTTON_DPAD_DOWN, 6); B(SDL_CONTROLLER_BUTTON_DPAD_LEFT, 7);
    B(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 10); B(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 11);
    B(SDL_CONTROLLER_BUTTON_Y, 12); B(SDL_CONTROLLER_BUTTON_B, 13); B(SDL_CONTROLLER_BUTTON_A, 14); B(SDL_CONTROLLER_BUTTON_X, 15);
#undef B
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) m |= 1u << 8;
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) m |= 1u << 9;
    int ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    if (ay < -16000) m |= 1u << 4; if (ax > 16000) m |= 1u << 5; if (ay > 16000) m |= 1u << 6; if (ax < -16000) m |= 1u << 7;
    return m;
}
int video_init(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 0; }
    win = SDL_CreateWindow("Dragon Ball GT Final Bout (recomp)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { SDL_AudioSpec w = { 0 }, h; w.freq = 44100; w.format = AUDIO_S16SYS; w.channels = 2; w.samples = 1024; adev = SDL_OpenAudioDevice(NULL, 0, &w, &h, 0); if (adev) SDL_PauseAudioDevice(adev, 0); }
    gc_open();
    return win && ren;
}
static uint32_t keybit(SDL_Keycode k) {
    switch (k) {
    case SDLK_RETURN: return 1u << 3;  case SDLK_BACKSPACE: case SDLK_RSHIFT: return 1u << 0;
    case SDLK_UP: return 1u << 4;      case SDLK_RIGHT: return 1u << 5;
    case SDLK_DOWN: return 1u << 6;    case SDLK_LEFT: return 1u << 7;
    case SDLK_e: return 1u << 8;       case SDLK_r: return 1u << 9;
    case SDLK_q: return 1u << 10;      case SDLK_w: return 1u << 11;
    case SDLK_s: return 1u << 12;      case SDLK_x: return 1u << 13;   /* Triangulo, Circulo */
    case SDLK_z: return 1u << 14;      case SDLK_a: return 1u << 15;   /* Cross, Quadrado */
    }
    return 0;
}
int audio_queued(void) { return adev ? (int)SDL_GetQueuedAudioSize(adev) / 4 : -1; }
void audio_push(const int16_t* s, int n) { if (adev) SDL_QueueAudio(adev, s, n * 4); }
void video_present(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) exit(0);
        if (e.type == SDL_KEYDOWN && !e.key.repeat) keys |= keybit(e.key.keysym.sym);
        if (e.type == SDL_KEYUP) keys &= ~keybit(e.key.keysym.sym);
        if (e.type == SDL_CONTROLLERDEVICEADDED) gc_open();
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && gc && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc))) { SDL_GameControllerClose(gc); gc = NULL; gc_open(); }
    }
    padbits = gc_bits();
    pad_set(keys | padbits);
    int w, h; gpu_render(fb, &w, &h);
    if (!tex || w != tw || h != th) { if (tex) SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h); tw = w; th = h; }
    SDL_UpdateTexture(tex, NULL, fb, w * 4);
    SDL_RenderClear(ren);
    int ow, oh; SDL_GetRendererOutputSize(ren, &ow, &oh);
    float s = (float)ow / (w * (h < 480 ? 1 : 1)); float s2 = (float)oh / (h < 300 ? 240 : h) * (h < 300 ? 1 : 1);
    /* mantem proporcao 4:3 */
    int dw = ow, dh = ow * 3 / 4; if (dh > oh) { dh = oh; dw = oh * 4 / 3; }
    (void)s; (void)s2;
    SDL_Rect dst = { (ow - dw) / 2, (oh - dh) / 2, dw, dh };
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}
#else
int video_init(void) { return 0; }
void video_present(void) {}
int audio_queued(void) { return -1; }
void audio_push(const int16_t* s, int n) {}
#endif
