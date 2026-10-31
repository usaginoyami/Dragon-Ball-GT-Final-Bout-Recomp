# Final Bout recomp — primeira versao (marco 15)

<img width="1024" height="1024" alt="dbgtbout-1651430927647" src="https://github.com/user-attachments/assets/93ea423b-0928-4e91-84dc-80e516663c47" />


Gera C a partir do SLUS_004.93 (seu disco) e roda o boot do jogo.

## Usar
1. Extraia do SEU disco: SLUS_004.93 e os arquivos STEP00.BIN, STEP01.BIN, ... (pasta MENU).
2. ./build.sh SLUS_004.93 PASTA_DOS_STEP        (gera o C em gen/ e compila)
3. ./recomp_test SLUS_004.93 --bin "Dragon Ball GT - Final Bout.bin" [-cd] [-v]
Para adicionar overlays: uma linha em overlays.txt (NOME BASE_HEX RAIZES) e rode o build de novo.

## Estado (marco 4)
- Controlador de CD emulado nos registradores (0x1F801800-03), com INT1/2/3, comandos Init, Setmode, Setloc, SeekL, ReadN/ReadS, GetID etc., lendo setores do seu .bin.
- Interrupcoes: o runtime chama o trapIntr da propria biblioteca do jogo (via SetCustomExitFromException), que despacha os callbacks de CD e DMA.
- DMA: canais 3 (CD), 6 (ClearOTagR) e os demais concluem na hora.
- O jogo carrega STEP00.BIN em 0x8003DFA0 (CD real), o runtime reconhece o overlay pelo hash e executa o codigo traduzido dele.
- Hoje o jogo chega a tocar o video de abertura (LOGO.STR, setor 24): precisa de MDEC (decodificador de video) ou de um atalho para pular.
- Ainda falta: MDEC, GPU (desenho), GTE (3D), SPU (som real), traduzir STEP01..STEP999, entrada de controle.

## Mapa da biblioteca Sony (PsyQ) neste exe
CD:  CdInit 8001c908 | CdFlush 8001cabc | CdSetDebug 8001cadc | CdSync 8001cb5c (CD_sync 8001d704) | CdReady 8001cb7c (CD_ready 8001d984)
     CdSyncCallback 8001cb9c | CdReadyCallback 8001cbb4 | CdControl 8001cbcc | CdControlF 8001cd04 | CdControlB 8001ce30
     CD_cw 8001dc58 | CD_init 8001e318 | CD_datasync 8001e504 | CdSetMode-like 8001ee40/8001ee58 | leitura/ISO9660 ('CD001') 8001f028
Video: VSync 8001bbec | VSyncCallback 8001be60 (callback do jogo: 8001104c) | ResetGraph 800268a4 | DrawSync 80026d6c | SetDispMask 80026cc8
     ClearImage 80026ef8 | LoadImage 80027028 | StoreImage 8002708c | MoveImage 800270f0
     ClearOTag 800271bc | ClearOTagR 80027274 | DrawOTag 8002736c | PutDrawEnv 800273d8 | DrawOTagEnv 800274dc | PutDispEnv 80027638
Jogo: main 80010c54 | laco de estados 80010ea8 | producer da fila 80017790 | espera da fila 80016980 | pump da fila 80017890

## Aviso
O C gerado e derivado do jogo (protegido por direitos autorais): use so no seu PC e nao publique.
Publique apenas a ferramenta (recomp.py + runtime).


## Marco 5 (atual)
- Ganchos em `hle_hook`: pula o video LOGO.STR (STEP00, 0x8003e14c) e o substado de video STR do STEP01 (0x8003f24c avanca o substado).
- overlays.txt agora tem STEP00 e STEP01 (ambos base 8003dfa0, distinguidos pelo hash).
- Resultado: o jogo chega no substado 0x2xx do STEP01 (provavel tela de titulo/menu) e fica rodando o laco por frame.
- Falta: GPU (desenho + janela SDL), GTE, MDEC, SPU, controle, STEP02/03/40/999.

## Marco 6
- `gpu.c`: GPU em software (VRAM 1024x512, poligonos flat/gouraud/texturizados 4/8/15bpp, sprites, linhas, fill, copias de VRAM, DMA canal 2 em lista encadeada/bloco).
- Controle: InitPad guarda os buffers; preenchidos a cada vblank (pad digital, nada apertado). `--start-at N` aperta Start automaticamente a partir do quadro N (teste).
- Opcoes novas: `--shot N` salva `shot.ppm` (tela) e `vram.ppm` no quadro N; `--frames N` sai no quadro N; env `ALARM=segundos` (padrao 30), `GPU_LOG=n` imprime n comandos GP0.
- Resultado: tela de copyright e menu principal (logo Dragon Ball GT Final Bout, Goku SSJ) renderizam corretamente.
- Falta: janela SDL + teclado, GTE (combate 3D), SPU audio, MDEC, STEP02/03/40/999.

## Marco 7 — janela e teclado (SDL2)
- Build com SDL2: o `build.sh` detecta `sdl2-config` sozinho. No Windows use MSYS2 (MINGW64): `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 python`, depois `./build.sh SLUS_004.93 PASTA_DOS_STEP`. No Linux/WSL: `apt install gcc python3 libsdl2-dev`.
- Jogar: `./recomp_test SLUS_004.93 --bin Dragon_Ball_GT_-_Final_Bout.bin --janela`
- Teclas: setas = direcional, Z = X(Cross), X = Circulo, A = Quadrado, S = Triangulo, Q/W = L1/R1, E/R = L2/R2, Enter = Start, Backspace ou Shift direito = Select, Esc = sair.
- Aviso: ainda sem GTE (3D), audio e os overlays STEP02/03/40/999; a luta provavelmente vai travar.

## Marco 8 — GTE e primeira luta
- `gte.c`: GTE implementado (RTPS/RTPT, NCLIP, OP, MVMVA, NCS/NCT/NCCS/NCCT/NCDS/NCDT, CC, CDP, DCPL, DPCS/DPCT, INTPL, SQR, GPF/GPL, AVSZ3/4).
- overlays.txt agora tem STEP00, STEP01 e STEP40 (luta). A luta renderiza em 3D (512x240).
- Script de entrada para chegar na luta sem teclado (Goku x Goku, ~2 min em tempo real por causa do carregamento do CD):
  `--press START@700 --press CROSS@1500 --press CROSS@1900 --press CROSS@2300 --press CROSS@3000 --press START@3300 --press CROSS@3600`
  (os numeros sao quadros; os botoes duram 4 quadros). Depure com `--shot N` (salva shot_N.ppm), `DBGS=1`, `GPU_LOGAT=N`, `VERBOSEAT=N`, `DUMPAT=N`.
- Falta: audio (SPU), overlays STEP02/03/999, MDEC (videos), velocidade do carregamento de CD, conferir fidelidade do GTE.

## Marco 9
- `--cd-turbo N` acelera o CD em N vezes (ex.: `--cd-turbo 10`): a luta carrega em ~3400 quadros em vez de ~6700. Use junto com `--janela`.

## Marco 10 — audio (SPU)
- `spu.c`: 24 vozes ADPCM, envelope ADSR, pitch com interpolacao linear, volumes, key on/off, DMA canal 4 e FIFO manual. Saida pelo SDL (44,1 kHz estereo) com `--janela`. Sem reverb, sem ruido (noise) e sem XA/CD-DA ainda.
- Depuracao: `AUDIO_RAW=arquivo.raw` grava o PCM s16 estereo 44100 Hz.

## Marco 11 — MDEC, XA, overlays extras (parcial)
- `mdec.c` (decodificador MDEC) e `xa.c` (audio XA do CD) escritos e ligados (DMA0/1, regs 0x1f801820/24, Setfilter, roteamento de setores de audio XA). Depuracao: `MDECLOG=1`.
- Overlays STEP02/STEP03/STEP999 adicionados ao overlays.txt (raizes 8004aa98 / 80056d28 / 8003e1cc, da tabela de estados em 0x8002bd48). Ainda nao executados.
- Video: **ainda nao funciona**. Com `--video` o jogo configura o MDEC (cmds 40000001/60000000), le 2 setores do STR e encerra o filme sem decodificar nenhum quadro. Por padrao os videos continuam pulados (`--pular-video` e o padrao; `--video` tenta o real). A investigacao continua em 8003e268/8003e03c (STEP00).
- Corrigido: ReadN/ReadS nao cria mais uma segunda cadeia de leitura de setores. Regressao ok: o caminho menu -> luta continua funcionando.

## Marco 12 — correcoes de CD (video ainda parcial)
- Setloc agora so marca o destino; a posicao muda no ReadN/ReadS/Seek (como no hardware). Antes a leitura anterior continuava e comia os 2 primeiros setores do filme.
- ReadN/ReadS respondem com o bit de leitura (0x20) no status.
- Gancho em 0x800163b8 (tentativa de pegar quadro do STR): espera 150 us por tentativa, porque o limite de 2000 tentativas foi pensado para uma CPU lenta.
- Depuracao nova: `WATCH=ENDERECO_HEX` imprime quando a palavra muda (ex.: `WATCH=8003EECC` = estado do filme do STEP00), `-cdd` mostra Setloc e setores XA/descartados.
- Video (`--video`): agora o primeiro setor (frame 1, chunk 0) chega e o jogo le o cabecalho de 32 bytes, mas depois nao busca o corpo (2016 bytes) e trava esperando algo do CD. Proximo passo: ver qual condicao em libcd/libpress ele espera depois do cabecalho.
- Regressao ok: menu -> luta continua funcionando.

## Marco 13 — Memory card (Build-up / STEP03)
- HLE de eventos do kernel (OpenEvent/TestEvent/DeliverEvent...) e `_card_info/_card_load/_card_read/_card_write`.
- `_card_load` agora retorna sucesso (R[2]=1); o jogo formata o cartão e salva em `memcard1.mcd` (128 KB).
- Tela de Build-up (blocos 1-7, CURRENT CHARACTER) alcançada e renderizada.
- Janela de tolerância do CD fixada em 0.25 s.
- Pendente: vídeo real (--video), STEP02/STEP999, teste de áudio com ouvido humano.

## Marco 14 — Correções de interrupção e início do vídeo (STR/MDEC)
- `ReturnFromException` (B0:17) agora encerra a interrupção (longjmp), e a entrada da ISR devolve `setjmp != 0`. Antes cada IRQ reexecutava o init da libapi, zerando DICR e a tabela de callbacks de DMA (inclusive o de fim de DMA do CD usado no STR).
- DICR: flags de canal são setadas quando o bit de enable do canal está ligado; IRQ só se o master (bit 23) estiver ligado.
- CD: o setor só é liberado quando lido por inteiro (cabeçalho STR de 32 bytes + corpo de 2016).
- Overlays: só reavalia o hash quando o DMA do CD cai na área de overlays (dados do overlay mudam em execução).
- `overlays.txt`: STEP00 ganhou a raiz 8003ed04 (callback de fade do vídeo).
- Resultado: com `--video` o jogo lê os 5 setores do 1º quadro, o MDEC decodifica (quadro preto = início de fade) e o jogo segue para o STEP01. Os quadros seguintes ainda não são pedidos: o jogo encerra o vídeo após o 1º quadro (causa em investigação).
- Memory card (Marco 13) incluído. Depuração: DBGIO=1 (escritas em DICR/I_MASK/DPCR), TRACE_HDR=N (traça N chamadas após cada cabeçalho STR), MDECLOG=1 (mostra quadro e média).
- Regressão: menu → luta (STEP40) continua funcionando.

## Marco 15 — Vídeo STR/MDEC funcionando (`--video`)
- Causa do vídeo parar no 1º quadro: o setor seguinte era entregue dentro da mesma ISR, antes do callback de fim de DMA (8001f250) marcar o quadro como pronto; o 1º setor do quadro seguinte era descartado pela libcd. Agora `cd_poll` não entrega setor de dados enquanto `in_isr` (equivale aos ~13 ms do hardware entre setores).
- Resultado: a abertura da Bandai toca (206 quadros decodificados em 1700 vblanks, imagem correta). Regressão menu → luta (STEP40) OK.
- Tentativas descartadas: reagendar setores com `now+periodo` e ordenar a fila de eventos deixaram o carregamento normal lento (head-of-line); não usar.
- Depuração nova: DBGST=1 (parâmetros do StSetStream).
- Pendente: XA (áudio dos vídeos) ainda não validado de ouvido; STEP02/STEP999; janela SDL em tela real.

## Marco 16 — STEP02 e STEP999 validados

- Novas ferramentas de depuração: `DBGSTATE=1` (transições de estado/substate), `TRACE_SUB=<hex>`, `explore.sh` e `--estado HEX@QUADRO`
  (força a variável de estado 0x80034fdc e a flag de saída 0x80056878 do STEP01, para pular direto a outro overlay).
- `--estado 20@700` carrega e executa o overlay STEP02 (fundo de rocha com o dragão); `--estado 50@700` executa o STEP999 (céu/nuvens). Nenhum "[nao traduzido]".
- Pendente: a tela do torneio (STEP01 substate 0x218) não avança só com CROSS; o caminho normal até STEP02/STEP999 ainda não foi achado.
- Às vezes aparece "CD timeout" ao forçar o estado (timing do CD com `--cd-turbo`); não reproduz em toda execução.

### Notas de exploração (Marco 16, continuação)
- O "[travou] loop sem fim" que aparecia nos testes era o alarme padrão de 30 s do runtime. Use `ALARM=0` (ou um valor maior) em execuções longas.
- `DBGSUB=1` (com `DBGSTATE=1`) imprime também o substate do STEP01 a cada mudança.
- Tela do torneio: substate 0x218 é tratado por 80041d24. TRIANGLE leva a 0x219, que volta para 0x215; CROSS volta ao menu (0x20d). Esse caminho não chegou a STEP02/STEP999.
