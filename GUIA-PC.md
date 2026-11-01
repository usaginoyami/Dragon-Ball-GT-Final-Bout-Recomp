# Guia: rodar o recomp no seu PC (janela, teclado e som)

Você precisa do SEU disco: `Dragon_Ball_GT_-_Final_Bout.bin`. Nada do jogo vai para o GitHub.

## 1. Instalar as ferramentas
**Windows (MSYS2):** instale o MSYS2, abra o terminal "MSYS2 MINGW64" e rode:

    pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-python make

**Linux / WSL:**

    sudo apt install git gcc python3 libsdl2-dev

## 2. Baixar a ferramenta

    git clone -b marco-16 https://github.com/usaginoyami/Dragon-Ball-GT-Final-Bout-Recomp.git
    cd Dragon-Ball-GT-Final-Bout-Recomp

## 3. Extrair arquivos do disco
(Coloque o `.bin` na pasta `jogo/`.) Do seu `.bin`, extraia `SLUS_004.93` e os `STEP00.BIN`, `STEP01.BIN`, `STEP02.BIN`, `STEP03.BIN`, `STEP40.BIN`, `STEP999.BIN` (ficam na pasta MENU).
Uma opção é o `dumpsxiso` (do projeto mkpsxiso), que extrai tudo de um `.bin`:

    dumpsxiso Dragon_Ball_GT_-_Final_Bout.bin -x extraido

Coloque `SLUS_004.93` e os STEPxx.BIN numa pasta (ex.: `jogo/`).

## 4. Compilar (uns 2 minutos)

    ./build.sh jogo/SLUS_004.93 jogo

O build detecta o SDL2 sozinho. Se aparecer "pronto: ./recomp_test ...", deu certo.

## 5. Jogar

    ./recomp_test jogo/SLUS_004.93 --bin "jogo/NOME DO SEU DISCO.bin" --janela --cd-turbo 10 --video

Use o nome real do seu arquivo `.bin`, entre aspas se tiver espaços.
`--cd-turbo 10` acelera as cargas (tire para o tempo real do CD). Por padrão o vídeo da Bandai é pulado; `--video` liga o vídeo com som.

## Teclas
| Controle | Tecla |
|---|---|
| Direcional | setas |
| Cross (X) | Z |
| Circulo | X |
| Quadrado | A |
| Triangulo | S |
| L1 / L2 | Q / W |
| R1 / R2 | R / E |
| Start | Enter |
| Select | Backspace ou Shift direito |
| Sair | Esc |

## Controle (XInput / Xbox)
Controles Xbox/XInput (e outros que o SDL2 reconheça) funcionam sozinhos, ligados antes ou durante o jogo: A=Cross, B=Circulo, X=Quadrado, Y=Triangulo, LB/RB=L1/R1, LT/RT=L2/R2, Back=Select, Start=Start, direcional ou analogico esquerdo = direcional. O segundo controle conectado vira o jogador 2. O teclado continua funcionando junto.

Jogador 2 no teclado: I/J/K/L = direcional, P = Triangulo, O = Circulo, U = Cross, Y = Quadrado, T/G = L1/R1, H/N = L2/R2, Enter do teclado numerico = Start.

## Memory card
Os saves ficam em `memcard1.mcd` (na pasta onde você roda o jogo). Apague esse arquivo para começar com um cartão vazio.

## O que eu preciso que você teste
1. A janela abre e mostra o vídeo da Bandai e as logos com o som certo?
2. O menu responde ao teclado? Dá para ir de Start até a luta?
3. A luta tem som e fica em velocidade normal (60 quadros/s)?
4. Ficou travando, com o som picotando ou com tela preta em algum ponto? Diga em qual tela.

Se algo der erro, copie as últimas linhas do terminal e me mande.

## Problemas conhecidos no Windows
- O build usa GCC 14+ do MSYS2; o `build.sh` já tem as flags necessárias.
- Se a janela abrir e fechar na hora com `[nao traduzido]`, o `.bin` não foi encontrado: confira o nome do arquivo.

## Overlay de FPS

F3 liga/desliga o contador de FPS (canto superior esquerdo). Para iniciar ligado: `FPS=1 ./recomp_test ...`.
