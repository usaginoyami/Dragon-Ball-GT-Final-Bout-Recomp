# Dragon Ball GT: Final Bout — recompilação estática (PS1)

<img width="1024" height="1024" alt="dbgtbout" src="https://github.com/user-attachments/assets/93ea423b-0928-4e91-84dc-80e516663c47" />

Recompilação estática de **Dragon Ball GT: Final Bout** (PS1, NTSC-U, `SLUS-004.93`): o código MIPS do jogo é traduzido para C e compilado como um programa nativo de **Windows e Linux**, com GPU, GTE, SPU, MDEC/XA, CD e memory card emulados por um runtime próprio.

> **Aviso legal.** Este repositório contém **só a ferramenta**. Nenhum dado do jogo, disco ou código gerado do jogo está aqui. Você precisa do **seu próprio disco** (`.cue` + `.bin`). O programa gerado a partir do seu disco é derivado do jogo (protegido por direitos autorais): use só no seu PC e **não publique**.

## Estado atual (Marco 16)
Funciona de ponta a ponta no Windows, validado jogando:
- Vídeo de abertura (BANDAI + filme), título, menus, Battle (VS Com = modo história), VS Man, Tournament completo, Build-up (com save), Treino, Sound Menu.
- Áudio com reverb, 60 FPS, tela cheia, teclado e controle (XInput) para os jogadores 1 e 2.
- Memory card em arquivo (`memcard1.mcd`).

Linux: compila e roda (o desenvolvimento e os testes automáticos foram feitos em Linux, com SDL2), mas o jogo foi jogado de verdade só no Windows. Não validado: áudio dos vídeos (XA) de ouvido, outras regiões/versões do jogo e macOS.

## Como usar (1 clique, Windows 64 bits)
1. Baixe o pacote **FinalBout-Recomp-win64.zip** (em *Releases*) e extraia numa pasta simples (ex.: `C:\FinalBout`).
2. Dê dois cliques em **`Jogar.bat`** e escolha o seu `.cue` (ou `.bin`).
3. Na **primeira vez** ele extrai os arquivos do disco, recompila o jogo e compila (alguns minutos). Nas próximas, abre direto.

O pacote traz Python, o compilador Zig e a SDL2 embutidos; nada precisa ser instalado. Ele não contém nada do jogo.

### Linux (x86_64)
Baixe **FinalBout-Recomp-linux-x86_64.tar.gz** (em *Releases*), extraia e rode `./Jogar.sh /caminho/do/disco.cue`. Precisa só da SDL2 do sistema (`sudo apt install libsdl2-2.0-0`). A compilação e a abertura do jogo foram testadas em Linux sem tela (SDL em modo dummy); o jogo em si foi jogado só no Windows.

### Teclas
| | Jogador 1 | Jogador 2 |
|---|---|---|
| Direcional | Setas | I / J / K / L |
| Triângulo / Círculo | S / X | P / O |
| Xis / Quadrado | Z / A | U / Y |
| L1 / R1 | Q / W | T / G |
| L2 / R2 | E / R | H / N |
| Start / Select | Enter / Backspace | Enter do teclado numérico / Ctrl direito |

Controles XInput funcionam sozinhos (1º controle = P1, 2º = P2). **F11** ou **Alt+Enter**: tela cheia. **F3**: mostra FPS. **Esc**: sair.

Opções úteis no `Jogar.bat`: `--pular-video` (pula a abertura), `--reconstruir` (recompila do zero). Variáveis: `FPS=1` (FPS ligado desde o início), `BIOSLOG=1` (log das chamadas de BIOS).

## Compilar do código-fonte (desenvolvedores)
- **Windows (MSYS2 MINGW64):** veja [`GUIA-PC.md`](GUIA-PC.md).
- **Linux:** instale `gcc`, `python3` e `libsdl2-dev`; depois `./build.sh SLUS_004.93 PASTA_DOS_STEP` e `./recomp_test SLUS_004.93 --bin disco.bin --janela`.
- **Pacote de 1 clique:** `kit/montar_kit.py` monta o zip (veja o cabeçalho do script).

Arquivos do disco necessários: `SLUS_004.93` e `STEP00/01/02/03/40/999.BIN` (pasta `MENU`). O lançador do pacote os extrai sozinho (`kit/iso.py`).

## Como funciona
- `recomp.py`: lê o executável/overlays MIPS e gera C (uma função por rotina).
- `build.py` + `overlays.txt`: gera o jogo e as tabelas de overlays (identificados por hash em tempo de execução).
- Runtime: `runtime.c` (CPU, BIOS emulada, memory card), `hw.c` (CD, DMA, timers), `gpu.c`, `gte.c`, `spu.c` (som + reverb), `mdec.c`/`xa.c` (vídeo e áudio de vídeo), `video.c` (janela SDL2, controles, FPS).
- Histórico técnico completo, marco a marco: [`docs/HISTORICO.md`](docs/HISTORICO.md).

## Outros projetos relacionados
Este projeto é independente e de aprendizado. Se você quer uma solução mais madura e com mais jogos, veja o [**psxrecomp**](https://github.com/mstan/psxrecomp) (mstan, licença PolyForm Noncommercial): um framework de recompilação estática para PS1, com BIOS recompilada, GPU em OpenGL e vários jogos, **inclusive o Dragon Ball GT: Final Bout** (com versus online/LAN). Aqui o foco é uma recompilação pequena e fácil de ler, com runtime próprio (BIOS por HLE, GPU em software) e um pacote de 1 clique.

## Depuração
`-cd` (log de CD), `-v` (rastro de chamadas), `DBGSTATE=1`/`DBGSUB=1` (estados do jogo), `GPU_LOADS=1` (uploads de VRAM), `MDECLOG=1`, `ALARM=0` (desliga o watchdog de 30 s), `--cd-turbo N` (carregamentos mais rápidos; vídeo e XA ficam em velocidade real).
