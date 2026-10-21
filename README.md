[🇺🇸 English](#english) | [🇧🇷 Português](#português)

---

# <a id="english"></a>BLACK PC port

An unofficial native PC port for Windows and Linux of **BLACK** (Criterion Games, 2006), made from the original Xbox version (NTSC-U).

`black.exe` (Windows) and `black_linux` (Linux) load your own copy of the game's `default.xbe` and run the original x86 game code directly on PC hardware without full system emulation.

**You need your own copy of the game.** This repository and the release downloads contain no game code or copyrighted assets.

---

## Features & Status

The game is fully playable from start to finish on both Windows and Linux. Please report problems in the issue tracker and include `black.log`.

- **Direct x86 Execution:** The original 32-bit Pentium III machine code runs natively on the host CPU at full hardware speed.
- **Modern Resolutions:** Any render resolution (720p, 1080p, 1440p, 4K and beyond, or native window scaling) with anti-aliasing.
- **Aspect Ratio & FOV:** Widescreen 16:9 (the game's own anamorphic widescreen mode) or 4:3, with adjustable field of view (50° to 120°) that preserves viewmodel proportions.
- **60 FPS & 30 FPS:** 60 fps (default) or the original 30 fps, with weapon refire rates, reload blur and physics paced correctly.
- **Precise Mouse Look:** Mouse look rotates the camera directly (scaled with scope zoom), with dynamic weapon sway mathematically restored from stick curves.
- **Rebindable Controls:** Keyboard and mouse bindings mapped directly to in-game actions, with full support for XInput and SDL2 gamepads.
- **Integrated PC Settings:** Options menu seamlessly injected into the game's original Flash/Apt UI (Options > PC SETTINGS and in the pause menu).
- **Audio & Visual Fixes:** Room reverb (I3DL2 FDN network), original gamma ramp and brightness slider, auto-aim toggle, movie skipping, seam filling for destructible geometry, and blur grid filtering.

---

## Requirements

### For Windows:
- Windows 10 or 11 (64-bit). The executable is 32-bit.
- A GPU with Direct3D 9 (Shader Model 3) support.
- Direct3D 9 runtime, shader compiler and XAudio2 (included with Windows).

### For Linux:
- Any modern 64-bit Linux distribution (Ubuntu 22.04+, Fedora, Arch Linux, Debian, SteamOS, etc.).
- 32-bit multilib runtime libraries (`glibc.i686`, `libstdc++.i686`).
- SDL2 (32-bit) and Vulkan 1.1+ compatible graphics driver (Mesa RADV/RadeonSI, Intel ANV, or NVIDIA).
- No Wine, Proton, or containers required when using the native `black_linux` binary!

### For Both:
- The **NTSC-U (North America)** Xbox disc of BLACK. Other regions and builds have different code offsets and are **not** supported.

---

## Game Preparation (Dump & Verification)

1. Dump your NTSC-U Xbox disc to an ISO and extract its files using [extract-xiso](https://github.com/XboxDev/extract-xiso):
   ```bash
   extract-xiso -x "Black (USA).iso"
   ```
   Rename the extracted folder to `game`. The folder must contain `default.xbe` along with all game asset directories (`levels/`, `sound/`, `export/`, `data/`, `GlobData.bin`, etc.).

2. Verify that you have the supported retail build:
   - **On Windows (PowerShell):**
     ```powershell
     Get-FileHash game\default.xbe -Algorithm SHA256
     ```
   - **On Linux (Bash):**
     ```bash
     sha256sum game/default.xbe
     ```
   The hash **must** be:
   `df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6`

---

## Playing on Linux

### Option 1: Native Linux Executable (`black_linux`) [Recommended]

The repository includes a 100% native Linux 32-bit ELF binary that interfaces directly with Linux syscalls, SDL2, and DXVK-Native (D3D9 over Vulkan).

1. Install runtime dependencies:
   - **Ubuntu / Debian:**
     ```bash
     sudo dpkg --add-architecture i386
     sudo apt update
     sudo apt install libsdl2-2.0-0:i386 libvulkan1:i386
     ```
   - **Arch Linux:**
     ```bash
     sudo pacman -S lib32-sdl2 lib32-vulkan-icd-loader
     ```
   - **Fedora:**
     ```bash
     sudo dnf install SDL2.i686 vulkan-loader.i686
     ```

2. Folder layout:
   - **Standalone Release Package (recommended for players):**
     Extract the release archive alongside your `game` directory:
     ```text
     BLACK/
     ├── game/
     │   ├── default.xbe
     │   ├── GlobData.bin
     │   ├── chars/
     │   ├── data/
     │   ├── export/
     │   ├── language/
     │   ├── levels/
     │   ├── sound/
     │   └── videos/
     └── port/
         ├── black_linux
         ├── lib/
         ├── README.md
         ├── KEYBINDS.md
         ├── black.ini    (auto-generated settings)
         ├── black.log    (runtime log)
         └── savedata/    (auto-generated save games)
     ```
   - **Development Build (running from source repository):**
     ```text
     black-pc/
     ├── black_linux
     ├── game/
     │   ├── default.xbe
     │   └── ... (all game assets)
     ├── savedata/
     └── tools/
     ```

3. Launch the game:
   - From the standalone `port` folder:
     ```bash
     cd port && ./black_linux
     ```
   - Or from the repository root:
     ```bash
     ./black_linux game/default.xbe
     ```

### Option 2: Running via Wine / Steam Deck Proton

If you prefer to run the Windows build (`black.exe` + `blackrt.dll`):
- **Desktop Wine:** Run with `wine black.exe` (DXVK recommended).
- **Steam Deck / Steam:** Add `black.exe` as a Non-Steam Game, set compatibility to Proton Experimental or GE-Proton, and launch.

---

## Playing on Windows

1. Download the release archive and extract it alongside your `game` directory:
   ```text
   BLACK/
   ├── game/
   │   ├── default.xbe
   │   ├── GlobData.bin
   │   ├── chars/
   │   ├── data/
   │   ├── export/
   │   ├── language/
   │   ├── levels/
   │   ├── sound/
   │   └── videos/
   └── port/
       ├── black.exe
       ├── blackrt.dll
       ├── README.md
       ├── KEYBINDS.md
       ├── black.ini    (auto-generated settings)
       ├── black.log    (runtime log)
       └── savedata/    (auto-generated save games)
   ```
2. Run `port\black.exe` (or double-click it in Windows Explorer). The loader will automatically detect `..\game\default.xbe` and store save games in `port\savedata\`.

---

## Controls

Keys and mouse buttons are bound to game actions (not controller buttons) and can be fully customized in **PC SETTINGS > CONTROLS > KEY BINDINGS**. Settings are saved to `black.ini`.

| Action | Default PC Key | Gamepad (Xbox / Generic) |
| :--- | :--- | :--- |
| **Move** | `W` `A` `S` `D` | Left Stick |
| **Look / Aim View** | Mouse movement | Right Stick |
| **Fire** | Left Mouse Button | Right Trigger (`RT`) |
| **Aim Down Sights** | Right Mouse Button | Right Stick Click (`RS`) |
| **Reload** | `R` | `A` Button |
| **Action (Use / Pick Up)** | `E` | `X` Button |
| **Crouch** | `C` | Left Stick Click (`LS`) |
| **Grenade** | `G` | Left Trigger (`LT`) |
| **Melee Attack** | `F` | `B` Button |
| **Next / Previous Weapon** | Mouse Wheel Down / Up | D-Pad Right / Left |
| **Change Firing Mode** | `Z` | D-Pad Up |
| **Toggle Suppressor** | `Q` | `Y` Button |
| **Health Pack** | `X` | D-Pad Down |
| **Show Objectives** | `Space` | `Back` Button |
| **Pause Menu** | `Esc` | `Start` Button |

- **Mouse Capture:** Click inside the window to capture the cursor. Press `Esc` to release it (and pause in gameplay).
- **Menu Navigation:** Arrow keys move between options, `Enter` selects, `Backspace` goes back. `Enter` also skips logos, briefings, and cutscenes.
- For the complete keybindings reference and `.ini` syntax, see **[KEYBINDS.md](KEYBINDS.md)**.

---

## Settings & In-Game Menus

Open the game menus (**Options > PC SETTINGS** on the main menu, or **PC SETTINGS** in the pause menu):

| Page | Available Settings |
| :--- | :--- |
| **VIDEO** | Resolution (Window, 720p, 1080p, 1440p, 4K, Native), Anti-Aliasing, Field of View (70°-120°), Brightness, Gamma, Borderless Fullscreen |
| **DISPLAY** | Aspect Ratio (16:9 / 4:3), Frame Rate (60 / 30 fps), Geometry Seam Fill, Effects Resolution |
| **AUDIO AND GAME** | Room Reverb Gain, Auto-Aim toggle, Skip Any Movie toggle |
| **CONTROLS** | Mouse Sensitivity, Mouse Invert, Rebindable Key Configuration |

---

## Command-Line Options

Command-line flags override settings in `black.ini` for the current session:

| Flag | Description |
| :--- | :--- |
| `--fullscreen` | Launch directly in borderless fullscreen mode |
| `--fps 30\|60` | Target frame rate (default: 60) |
| `--resolution WxH\|native` | Internal render resolution (e.g. `1920x1080`, `2560x1440`, `native`) |
| `--scale <factor>` | Render scale multiplier (e.g. `1.5`, `2.0`, `3.0`) |
| `--window WxH` | Host window dimensions (default: `1280x720`) |
| `--fov <degrees>` | Horizontal field of view in degrees (50 to 120, default: 70) |
| `--autoaim` / `--no-autoaim` | Enable or disable auto-aim pull towards enemies (default: disabled) |
| `--skip-movies` | Allow skipping cutscenes and logos with `Enter` or `Space` (default: enabled) |
| `--no-movie-skip` | Enforce original Xbox rule (only completed mission movies can be skipped) |
| `--hud`, `--show-fps` | Enable DXVK on-screen Vulkan overlay HUD (framerate, frametimes) |
| `--keybinds` | Print the active keybindings table and exit |
| `--info` | Print parsed XBE headers, sections, and title information and exit |
| `--trace` | Log all Xbox kernel ordinal invocations |
| `--trace-d3d` | Trace Direct3D API calls |
| `--force` | Bypass SHA-256 build verification check |
| `--help`, `-h` | Display help and usage message |

---

## Building from Source

### Building on Linux (`black_linux`):
Requires GCC/G++ with 32-bit multilib support (`gcc -m32`), GNU Make, and 32-bit development headers:
- **Ubuntu/Debian:** `sudo apt install build-essential gcc-multilib g++-multilib libsdl2-dev:i386`
- **Arch Linux:** `sudo pacman -S base-devel multilib-devel lib32-sdl2`
- **Fedora:** `sudo dnf install gcc gcc-c++ glibc-devel.i686 libstdc++-devel.i686 SDL2-devel.i686`

Compile with:
```bash
make -j$(nproc)
```
Output: `./black_linux` (ready to run).

### Building on Windows (`black.exe` + `blackrt.dll`):
Requires Visual Studio 2022 or 2026 with C++ x86 desktop development tools and CMake 3.24+.
```cmd
cd loader
cmake --preset win32
cmake --build build/win32 --config RelWithDebInfo
```
Output: `loader\build\win32\RelWithDebInfo\black.exe` and `blackrt.dll`.

---

## Architecture: How It Works Under the Hood

Unlike conventional full-system emulators (*xemu*, *Cxbx-Reloaded*) or source recompilations:

1. **Direct Execution:** The Xbox CPU was an Intel Pentium III (x86 32-bit). On PC x86, the loader maps `default.xbe` at its fixed virtual address (`0x00010000`) and the host CPU directly executes the game's original machine code.
2. **KPCR & Threading:** On Linux, glibc uses `%gs` for TLS, leaving `%fs` free. The loader sets up a custom LDT segment descriptor via `SYS_modify_ldt` so that `%fs` directly points to each thread's Xbox `KPCR` (Kernel Processor Control Region). On Windows, instructions referencing `%fs` are redirected to TEB TLS slots.
3. **Graphics Translation:** NV2A vertex shader microcode and pixel shader register combiners are dynamically disassembled into HLSL (`vs_2_0` / `ps_2_0`) and translated to Vulkan via DXVK-Native on Linux (or Direct3D 9 on Windows).
4. **HLE DirectSound:** The hardware MCPX audio processor is replaced with an HLE software mixer decoding PCM and Xbox ADPCM, driving 3D audio rolloff, I3DL2 room reverb, and outputting 48 kHz stereo to SDL2 Audio / XAudio2.
5. **Flash/Apt Menu Hijack:** The loader disassembles the game's Macromedia Flash 7 / EA Apt UI containers (`FEMain.bin`, `PseMenu.bin`), injects custom ActionScript 2 bytecode, and serves the modified menus with `PC SETTINGS` via the Virtual File System.

---

## Legal & Credits

This project is an independent open-source research and preservation effort. It is not affiliated with, sponsored by, or endorsed by Criterion Games, Electronic Arts, or Microsoft Corporation.
- **BLACK** is a registered trademark of Electronic Arts Inc.
- **Xbox** is a registered trademark of Microsoft Corporation.
- No copyrighted game code, assets, ROMs, or SDKs are distributed in this repository.

Developed by **Spark198rus** and open-source contributors.

---
---

# <a id="português"></a>BLACK PC port (Português)

Um port nativo para PC (Windows e Linux) de **BLACK** (Criterion Games, 2006), feito a partir da versão original de Xbox (NTSC-U).

O executável `black.exe` (Windows) e `black_linux` (Linux) carregam a sua própria cópia do arquivo `default.xbe` do jogo e executam o código de máquina x86 original diretamente no processador do seu computador, sem emulação de máquina completa.

**Você precisa ter a sua própria cópia do jogo.** Este repositório e os downloads de releases não contêm nenhum código ou arquivo proprietário do jogo original.

---

## Funcionalidades e Status

O jogo é 100% jogável do início ao fim tanto no Windows quanto no Linux. Caso encontre algum problema, abra uma issue anexando o arquivo `black.log`.

- **Execução Direta x86:** O código de máquina Pentium III de 32 bits original roda diretamente na CPU host em velocidade nativa.
- **Resoluções Modernas:** Qualquer resolução de renderização (720p, 1080p, 1440p, 4K e além, ou escala nativa da janela) com anti-aliasing.
- **Proporção de Tela & FOV:** Widescreen 16:9 (o modo anamórfico original do jogo) ou 4:3, com campo de visão (FOV) ajustável (50° a 120°) mantendo as proporções das armas.
- **60 FPS e 30 FPS:** 60 fps (padrão) ou os 30 fps originais, com cadência de tiro das armas, rastro de recarga e física calculados no tempo correto.
- **Mira Precisa com o Mouse:** A visão pelo mouse gira a câmera diretamente (com sensibilidade ajustada no zoom da mira), mantendo o balanço físico natural da arma (*weapon sway*) restaurado matematicamente.
- **Controles Reconfiguráveis:** Teclado e mouse mapeados diretamente para ações do jogo, além de suporte plug-and-play para controles XInput e SDL2 (Xbox, PlayStation, genéricos).
- **Menu PC SETTINGS Integrado:** Menus de configuração do port injetados diretamente na interface original em Flash/Apt do jogo (Options > PC SETTINGS e no menu de pausa).
- **Correções Visuais e Sonoras:** Reverberação de ambiente I3DL2 (rede FDN), rampa de brilho e gama original, opção de desativar mira automática (auto-aim), pulo de cutscenes, preenchimento de costuras geométricas em objetos destrutíveis e filtro de grade no efeito de blur.

---

## Requisitos de Sistema

### Para Windows:
- Windows 10 ou 11 (64 bits). O executável roda como processo de 32 bits.
- Placa de vídeo com suporte a Direct3D 9 (Shader Model 3).
- Direct3D 9, compilador de shaders e XAudio2 (já inclusos no Windows).

### Para Linux:
- Qualquer distribuição Linux moderna de 64 bits (Ubuntu 22.04+, Fedora, Arch Linux, Debian, SteamOS, etc.).
- Bibliotecas multilib de 32 bits instaladas (`glibc.i686`, `libstdc++.i686`).
- SDL2 (32-bit) e driver gráfico compatível com Vulkan 1.1 ou superior (Mesa RADV/RadeonSI, Intel ANV ou NVIDIA).
- **Nenhum Wine, Proton ou contêiner é necessário** ao utilizar o binário nativo `black_linux`!

### Para Ambos:
- O disco original de Xbox de BLACK na versão **NTSC-U (América do Norte)**. Outras regiões e builds possuem ponteiros de código diferentes e **não** são suportadas.

---

## Preparação do Jogo (Dump e Verificação)

1. Extraia o conteúdo da ISO do jogo usando a ferramenta [extract-xiso](https://github.com/XboxDev/extract-xiso):
   ```bash
   extract-xiso -x "Black (USA).iso"
   ```
   Renomeie a pasta extraída para `game`. A pasta deve conter o executável `default.xbe` acompanhado de todas as pastas e arquivos de assets do jogo (`levels/`, `sound/`, `export/`, `data/`, `GlobData.bin`, etc.).

2. Verifique se a sua cópia corresponde à build suportada:
   - **No Windows (PowerShell):**
     ```powershell
     Get-FileHash game\default.xbe -Algorithm SHA256
     ```
   - **No Linux (Bash):**
     ```bash
     sha256sum game/default.xbe
     ```
   O hash resultante **deve ser obrigatoriamente**:
   `df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6`

---

## Como Jogar no Linux

### Opção 1: Executável Nativo Linux (`black_linux`) [Recomendado]

O projeto disponibiliza um binário ELF de 32 bits totalmente nativo para Linux, comunicando-se diretamente com o kernel Linux, SDL2 e DXVK-Native (D3D9 sobre Vulkan).

1. Instale as dependências de 32 bits da sua distribuição:
   - **Ubuntu / Debian:**
     ```bash
     sudo dpkg --add-architecture i386
     sudo apt update
     sudo apt install libsdl2-2.0-0:i386 libvulkan1:i386
     ```
   - **Arch Linux:**
     ```bash
     sudo pacman -S lib32-sdl2 lib32-vulkan-icd-loader
     ```
   - **Fedora:**
     ```bash
     sudo dnf install SDL2.i686 vulkan-loader.i686
     ```

2. Estrutura de pastas:
   - **Pacote de Release Standalone (recomendado para jogadores):**
     Extraia o pacote de release ao lado da pasta `game`:
     ```text
     BLACK/
     ├── game/
     │   ├── default.xbe
     │   ├── GlobData.bin
     │   ├── chars/
     │   ├── data/
     │   ├── export/
     │   ├── language/
     │   ├── levels/
     │   ├── sound/
     │   └── videos/
     └── port/
         ├── black_linux
         ├── lib/
         ├── README.md
         ├── KEYBINDS.md
         ├── black.ini    (gerado na primeira execução)
         ├── black.log    (log de execução)
         └── savedata/    (gerado automaticamente para os saves)
     ```
   - **Ambiente de Desenvolvimento (executando direto do código-fonte):**
     ```text
     black-pc/
     ├── black_linux
     ├── game/
     │   ├── default.xbe
     │   └── ... (todos os assets do jogo)
     ├── savedata/
     └── tools/
     ```

3. Inicie o jogo pelo terminal:
   - A partir da pasta standalone `port`:
     ```bash
     cd port && ./black_linux
     ```
   - Ou da raiz do repositório:
     ```bash
     ./black_linux game/default.xbe
     ```

### Opção 2: Execução via Wine / Proton (Steam Deck)

Se preferir rodar a versão de Windows (`black.exe` + `blackrt.dll`):
- **Wine no Desktop:** Execute com `wine black.exe` (DXVK recomendado).
- **Steam Deck / Steam:** Adicione `black.exe` como Jogo Não-Steam, ative a compatibilidade com Proton Experimental ou GE-Proton e inicie normalmente.

---

## Como Jogar no Windows

1. Baixe a release e extraia o pacote ao lado da pasta `game`:
   ```text
   BLACK/
   ├── game/
   │   ├── default.xbe
   │   ├── GlobData.bin
   │   ├── chars/
   │   ├── data/
   │   ├── export/
   │   ├── language/
   │   ├── levels/
   │   ├── sound/
   │   └── videos/
   └── port/
       ├── black.exe
       ├── blackrt.dll
       ├── README.md
       ├── KEYBINDS.md
       ├── black.ini    (gerado na primeira execução)
       ├── black.log    (log de execução)
       └── savedata/    (gerado automaticamente para os saves)
   ```
2. Execute `port\black.exe` (ou dê duplo clique pelo Explorador de Arquivos do Windows). O loader localizará automaticamente os assets em `..\game\default.xbe` e manterá os saves e configurações em `port\savedata\`.

---

## Controles

As teclas e botões do mouse são vinculados diretamente às ações do jogo e podem ser alterados em **PC SETTINGS > CONTROLS > KEY BINDINGS**. As opções ficam salvas em `black.ini`.

| Ação | Teclado e Mouse Padrão | Gamepad (Xbox / Genérico) |
| :--- | :--- | :--- |
| **Mover-se** | `W` `A` `S` `D` | Alavanca Esquerda |
| **Olhar / Mirar** | Movimento do Mouse | Alavanca Direita |
| **Atirar** | Botão Esquerdo do Mouse | Gatilho Direito (`RT`) |
| **Visada com a Mira** | Botão Direito do Mouse | Clique Analógico Direito (`RS`) |
| **Recarregar** | `R` | Botão `A` |
| **Ação (Pegar / Usar)** | `E` | Botão `X` |
| **Agachar** | `C` | Clique Analógico Esquerdo (`LS`) |
| **Lançar Granada** | `G` | Gatilho Esquerdo (`LT`) |
| **Ataque Corpo a Corpo** | `F` | Botão `B` |
| **Arma Seguinte / Anterior**| Roda do Mouse (Baixo / Cima) | Direcional Digital Direita / Esquerda |
| **Mudar Modo de Disparo** | `Z` | Direcional Digital Cima |
| **Silenciador (Colocar/Tirar)**| `Q` | Botão `Y` |
| **Kit de Primeiros Socorros** | `X` | Direcional Digital Baixo |
| **Mostrar Objetivos** | `Espaço` | Botão `Back` |
| **Menu de Pausa** | `Esc` | Botão `Start` |

- **Captura do Mouse:** Clique dentro da janela para prender o cursor. Pressione `Esc` para soltá-lo (e pausar o jogo durante a partida).
- **Navegação nos Menus:** Setas direcionais movem o foco, `Enter` confirma e `Backspace` volta. `Enter` também avança a tela "PRESS START" e pula vídeos.
- Para uma tabela completa de atalhos e sintaxe do `.ini`, consulte o arquivo **[KEYBINDS.md](KEYBINDS.md)**.

---

## Menus e Configurações

Acesse os ajustes dentro dos próprios menus do jogo (**Options > PC SETTINGS** ou no menu de pausa):

| Página | Opções Disponíveis |
| :--- | :--- |
| **VIDEO** | Resolução (Janela, 720p, 1080p, 1440p, 4K, Nativo), Anti-Aliasing, Campo de Visão (70°-120°), Brilho, Gama, Tela Cheia Sem Bordas |
| **DISPLAY** | Proporção de Tela (16:9 / 4:3), Taxa de Quadros (60 / 30 fps), Preenchimento de Costuras, Efeitos no Tamanho Original |
| **AUDIO AND GAME** | Ganho do Reverb de Sala, Mira Automática (Auto-Aim), Pular Qualquer Cutscene |
| **CONTROLS** | Sensibilidade do Mouse, Inversão do Mouse, Mapeamento Completo de Teclas |

---

## Opções por Linha de Comando

Parâmetros passados via linha de comando têm prioridade sobre o `black.ini` na sessão atual:

| Parâmetro | Descrição |
| :--- | :--- |
| `--fullscreen` | Inicia diretamente em tela cheia sem bordas |
| `--fps 30\|60` | Taxa de quadros alvo (padrão: 60) |
| `--resolution WxH\|native` | Resolução interna (ex: `1920x1080`, `2560x1440`, `native`) |
| `--scale <fator>` | Fator de multiplicação de resolução (ex: `1.5`, `2.0`, `3.0`) |
| `--window WxH` | Dimensões da janela host (padrão: `1280x720`) |
| `--fov <graus>` | Campo de visão horizontal em graus (50 a 120, padrão: 70) |
| `--autoaim` / `--no-autoaim` | Ativa ou desativa a atração de mira para inimigos (padrão: desativado) |
| `--skip-movies` | Permite pular cutscenes e logos com `Enter` ou `Espaço` (padrão: ativado) |
| `--no-movie-skip` | Mantém a regra do Xbox (só pula vídeos de missões já concluídas) |
| `--hud`, `--show-fps` | Ativa o overlay Vulkan do DXVK (taxa de quadros e tempos de quadro) |
| `--keybinds` | Imprime a tabela atual de mapeamento de teclas no terminal e sai |
| `--info` | Imprime os cabeçalhos, seções e dados do XBE e encerra |
| `--trace` | Registra no log todas as chamadas de ordinais do kernel |
| `--trace-d3d` | Registra no log as chamadas da API Direct3D |
| `--force` | Ignora a verificação de hash SHA-256 do `default.xbe` |
| `--help`, `-h` | Exibe a mensagem de ajuda e opções disponíveis |

---

## Compilação do Código-Fonte

### Compilando no Linux (`black_linux`):
Requer GCC/G++ com suporte a multilib de 32 bits (`gcc -m32`), GNU Make e headers de desenvolvimento de 32 bits da SDL2:
- **Ubuntu/Debian:** `sudo apt install build-essential gcc-multilib g++-multilib libsdl2-dev:i386`
- **Arch Linux:** `sudo pacman -S base-devel multilib-devel lib32-sdl2`
- **Fedora:** `sudo dnf install gcc gcc-c++ glibc-devel.i686 libstdc++-devel.i686 SDL2-devel.i686`

Compile executando:
```bash
make -j$(nproc)
```
O executável compilado será gerado como `./black_linux`.

### Compilando no Windows (`black.exe` + `blackrt.dll`):
Requer Visual Studio 2022 ou 2026 com ferramentas C++ x86 e CMake 3.24+:
```cmd
cd loader
cmake --preset win32
cmake --build build/win32 --config RelWithDebInfo
```
Os arquivos gerados serão `loader\build\win32\RelWithDebInfo\black.exe` e `blackrt.dll`.

---

## Arquitetura: Como Funciona por Baixo dos Panos

Diferente de emuladores convencionais de sistema (*xemu*, *Cxbx-Reloaded*) ou recriações completas de código-fonte:

1. **Execução Direta:** A CPU do Xbox original era um Intel Pentium III (x86 de 32 bits). Em PCs x86, o loader mapeia o `default.xbe` em seu endereço virtual fixo (`0x00010000`) e o processador do computador executa o código binário original diretamente.
2. **KPCR e Threading:** No Linux de 32 bits, a glibc utiliza `%gs` para TLS, deixando `%fs` livre. O loader configura uma entrada na tabela LDT via `SYS_modify_ldt`, fazendo `%fs` apontar diretamente para a estrutura `KPCR` (Kernel Processor Control Region) de cada thread. No Windows, instruções `fs:` são redirecionadas para slots de TLS do TEB.
3. **Tradução Gráfica:** A microcode do vertex shader da GPU NV2A e os combinadores de pixel shader são desmontados e traduzidos dinamicamente para HLSL (`vs_2_0` / `ps_2_0`), sendo renderizados via Vulkan pelo DXVK-Native no Linux (ou Direct3D 9 no Windows).
4. **DirectSound HLE:** O processador de áudio MCPX é substituído por um mixer em software de alto desempenho que decodifica PCM e Xbox ADPCM, calcula atenuação 3D e reverberação I3DL2, enviando blocos de áudio de 48 kHz estéreo para a SDL2 Audio / XAudio2.
5. **Injeção de Menus Flash Apt:** O loader desmonta os arquivos de interface SWF do Flash 7 da EA (`FEMain.bin`, `PseMenu.bin`), injeta novo bytecode ActionScript 2 com as páginas `PC SETTINGS` e monta os arquivos alterados através do VFS.

---

## Legal e Créditos

Este projeto é uma iniciativa independente de pesquisa e preservação de software. Não possui afiliação, patrocínio ou vínculo com a Criterion Games, Electronic Arts ou Microsoft Corporation.
- **BLACK** é marca registrada da Electronic Arts Inc.
- **Xbox** é marca registrada da Microsoft Corporation.
- Nenhum arquivo protegido por direitos autorais, código proprietário ou ROM está incluso neste repositório.

Desenvolvido por **Spark198rus** e contribuidores de código aberto.
