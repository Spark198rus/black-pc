[🇺🇸 English](#english) | [🇧🇷 Português](#português)

---

# <a id="english"></a>BLACK PC port

An unofficial native Windows port (and playable on Linux) of **BLACK** (Criterion Games, 2006), made from the original Xbox releases (NTSC-U and PAL Europe).

`black.exe` loads your own copy of the game's `default.xbe` and runs the original x86 game code directly on the PC.

**You need your own copy of the game.** This repository and the release downloads contain no game code or data.

## Status

The game is fully playable. Please report problems in the issue tracker and include `port\black.log` (or `port/black.log` on Linux).

- Any render resolution (720p to 4K and beyond, or a multiple of the window size), anti-aliasing
- Widescreen 16:9 (the game's own widescreen mode) or 4:3, adjustable field of view
- 60 fps (the default) or the original 30 fps
- Mouse look, with the weapon swaying as it does with the stick; keys bound to the game's actions; XInput controllers
- The port's settings in the game's own menus (Options > PC SETTINGS, and in the pause menu), including borderless fullscreen
- Room reverb, the game's own brightness and gamma ramp; auto-aim off by default; every movie can be skipped
- Fixes for things the Xbox hides by its low resolution: flickering seams between pieces of destructible objects, a grid pattern in the blur effects

## Requirements

**For Windows:**
- Windows 10 or 11. The program is 32-bit and runs on 64-bit Windows.
- A graphics card with Direct3D 9 (Shader Model 3). On old laptops with Intel graphics (HD 4400 class) play at 30 fps.
- Nothing else needs installing: the C runtime is built in, and Direct3D 9, the shader compiler and XAudio2 ship with Windows.

**For Linux:**
- A modern Linux distribution (Ubuntu, Fedora, Arch, Parrot OS, etc.).
- [Distrobox](https://distrobox.privatedns.org/) installed on the host system.
- WineHQ version 9+ (Staging) and DXVK (Vulkan).
- A graphics card supporting Vulkan 1.1 or higher (e.g., AMD Radeon Vega 8 or better).

**For both systems:**
- The NTSC-U (North America) or PAL Europe Xbox disc of BLACK. Other builds have different code addresses and are **not** supported.

---

## Windows Setup

1. Dump your NTSC-U or PAL Europe version of the game to an ISO and extract it with [extract-xiso](https://github.com/XboxDev/extract-xiso) (`extract-xiso -x Black.iso`). Rename the extracted folder to `game`.
2. Check that you have the supported build. In PowerShell:
   ```powershell
   Get-FileHash game\default.xbe -Algorithm SHA256
   ```
   It must print one of these:
   - NTSC-U: `DF2739C372D254A90AEECCF5971D12097F22D89FBB90DB021FDA96DD4DEB68F6`
   - PAL Europe: `67F35121CCF622E7A293C402219596FE6456CA4C3D27E6F8139E449264DA30F0`
3. Download the zip from [Releases](../../releases) and extract it next to the `game` folder, so the `port` folder sits beside it:
   ```text
   BLACK\
     game\
       default.xbe
       ...
     port\
       black.exe
       blackrt.dll
   ```
4. Start `port\black.exe`. 

The port keeps everything it writes in the `port` folder: `savedata\` holds the Xbox hard disk (saved games and the game's settings), `black.ini` the port's settings, `black.log` the log of the last run and `cache\menus` the game's menu files with the PC SETTINGS pages added.

---

## Linux Setup (Distrobox + Wine 9 + DXVK Guide)

To avoid rendering issues (such as the `hlsl.y` compiler crash in older Wine versions) and ensure maximum performance via Vulkan, follow these steps.

### Phase 1: Create the Base Container
Using Ubuntu 22.04 isolates the 32-bit dependencies without cluttering your main system.
1. Open your host machine's terminal and create the container:
   ```bash
   distrobox create --name gaming-env --image ubuntu:22.04
   ```
2. Enter the container:
   ```bash
   distrobox enter gaming-env
   ```

### Phase 2: Install WineHQ 9+ (Staging)
Modern Wine 9 eliminates chronic shader bugs found in legacy versions.
1. Enable 32-bit architecture:
   ```bash
   sudo dpkg --add-architecture i386
   ```
2. Download the official keys and add the Ubuntu Jammy repository:
   ```bash
   sudo mkdir -pm755 /etc/apt/keyrings
   sudo wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key
   sudo wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/jammy/winehq-jammy.sources
   ```
3. Update packages and install WineHQ Staging:
   ```bash
   sudo apt update
   sudo apt install --install-recommends winehq-staging -y
   ```

### Phase 3: Inject DXVK 1.10.3 (Vulkan Engine)
Version 1.10.3 ensures maximum Vulkan compatibility for integrated GPUs (like the AMD Vega line).
1. Download DXVK and extract it inside the container:
   ```bash
   wget https://github.com/doitsujin/dxvk/releases/download/v1.10.3/dxvk-1.10.3.tar.gz
   tar -zxf dxvk-1.10.3.tar.gz
   ```
2. Copy the 32-bit translation libraries directly to your game directory (adjust `/path/to/game/port` to your actual path):
   ```bash
   cp dxvk-1.10.3/x32/d3d9.dll /path/to/game/port/
   cp dxvk-1.10.3/x32/dxgi.dll /path/to/game/port/
   ```

### Phase 4: File Cleanup and Execution
Windows `.zip` archives extracted on Linux might generate files with literal backslashes (`\`) in their names. We must clean this up.
1. Navigate to the port folder:
   ```bash
   cd /path/to/game/port/
   ```
2. Rename the base files (removing the `port\` prefix if it exists):
   ```bash
   mv 'port\black.exe' black.exe
   mv 'port\blackrt.dll' blackrt.dll
   mv 'port\LICENSE.txt' LICENSE.txt
   mv 'port\README.md' README.md
   ```
3. Run the game, forcing Wine to use the native DXVK libraries we just copied:
   ```bash
   WINEDLLOVERRIDES="d3d9=n;dxgi=n" wine black.exe
   ```

---

## Controls

Keys and mouse buttons are bound to the game's own actions, not to controller buttons, and can be changed in PC SETTINGS > CONTROLS > KEY BINDINGS (two keys per action, saved to `black.ini`). Controllers work as well. Click the window to capture the mouse; Esc lets go of it (in play it also pauses), and play starting or resuming takes it again.

| Action | Default |
|---|---|
| Move | W A S D |
| Look | mouse |
| Fire / Aim | left / right mouse button |
| Reload | R |
| Action (use, pick up) | E |
| Crouch | C |
| Grenade | G |
| Melee | F |
| Next / previous weapon | mouse wheel down / up |
| Change firing mode | Z |
| Toggle suppressor | Q |
| Health pack | X |
| Show objectives | Space |
| Lean left / right (experimental) | V / B |
| Pause | Esc |

In menus the arrow keys move, Enter selects and Backspace goes back; Enter also passes PRESS START, skips movies and intro text, and closes hints. QUIT at the bottom of the main menu closes the game. 

## Settings

The port's settings are in the game's menus: Options > PC SETTINGS on the main menu, and PC SETTINGS in the pause menu.

| Page | Settings |
|---|---|
| VIDEO | render resolution, anti-aliasing, field of view, brightness, gamma, fullscreen |
| DISPLAY | aspect ratio, frame rate (main menu only), filling geometry seams, effects at Xbox size |
| TWEAKS | auto-aim, reload blur, lean angle, lean distance |
| AUDIO AND MOVIES | room reverb, skipping any movie, language (PAL disc; from the next start) |
| CONTROLS | mouse sensitivity, mouse inversion, key bindings |

`black.exe --help` lists the command-line options. They override `black.ini` for that run (e.g., `--fullscreen --resolution 1080 --fps 30 --fov 86`).

## Known issues
- The game was made for 30 fps; at 60 (the default) something may still run at the wrong speed.
- On-screen prompts show Xbox controller buttons, even when playing with a keyboard.
- Audio: the reverb is an approximation, some effect sends are not mixed, there is no subwoofer channel, and 3D sound uses distance and panning only.
- **Only the NTSC-U and PAL Europe builds listed above are supported.** On the PAL disc the game's language follows
  Windows (`--language en|de|fr|es|it` or `language =` in black.ini to choose).

## Building from source
Visual Studio 2022 or 2026 with C++ x86 tools, and CMake 3.24+.
```cmd
cd loader
cmake --preset win32            # VS 2026; use win32-vs2022 for VS 2022
cmake --build build/win32 --config RelWithDebInfo
```

## Legal & Credits
This project is not affiliated with Criterion Games or Electronic Arts. BLACK is a trademark of Electronic Arts Inc.; Xbox is a trademark of Microsoft Corporation. No original code or SDK is included. **You need your own legally obtained copy of the game.**
Developed by Spark198rus. Linux setup structured via AI.

---
---

# <a id="português"></a>BLACK PC port (Português)

Um port não oficial nativo para Windows (e executável no Linux) de **BLACK** (Criterion Games, 2006), feito a partir das versões originais de Xbox (NTSC-U e PAL Europa).

O executável `black.exe` carrega a sua própria cópia do arquivo `default.xbe` do jogo e roda o código x86 original diretamente no PC.

**Você precisa ter a sua própria cópia do jogo.** Este repositório e os downloads de release não contêm nenhum código ou dado do jogo original.

## Status

O jogo é totalmente jogável. Por favor, reporte problemas no rastreador de issues (issue tracker) e inclua o arquivo `port\black.log` (ou `port/black.log` no Linux).

- Qualquer resolução de renderização (de 720p a 4K e além), anti-aliasing.
- Widescreen 16:9 (o próprio modo widescreen nativo do jogo) ou 4:3, com campo de visão (FOV) ajustável.
- 60 fps (padrão) ou os 30 fps originais.
- Visão pelo mouse (Mouse look), com a arma balançando; teclas vinculadas às ações do jogo; suporte a controles XInput.
- Configurações do port integradas aos próprios menus do jogo (Options > PC SETTINGS e no menu de pausa), incluindo tela cheia sem bordas (borderless fullscreen).
- Reverberação de ambiente, rampa de brilho e gama do próprio jogo; mira automática (auto-aim) desativada por padrão; todas as cutscenes podem ser puladas.
- Correções para artefatos que o Xbox escondia devido à baixa resolução (costuras piscantes e grade no blur).

## Requisitos

**Para Windows:**
- Windows 10 ou 11. O programa é de 32 bits e roda no Windows de 64 bits.
- Uma placa de vídeo com Direct3D 9 (Shader Model 3).
- Nenhuma instalação extra é necessária (o Direct3D 9 e XAudio2 já vêm com o Windows).

**Para Linux:**
- Uma distribuição Linux moderna (Ubuntu, Fedora, Arch, Parrot OS, etc).
- [Distrobox](https://distrobox.privatedns.org/) instalado no sistema host.
- WineHQ versão 9+ (Staging) e DXVK (Vulkan).
- Placa de vídeo compatível com Vulkan 1.1 ou superior (ex: AMD Radeon Vega 8 ou superior).

**Para ambos os sistemas:**
- O disco original de BLACK do Xbox na versão NTSC-U (América do Norte) ou PAL Europa. Outras builds **não** são suportadas.

---

## Configuração no Windows

1. Faça o dump da sua versão NTSC-U ou PAL Europa do jogo para uma ISO e extraia-a usando o [extract-xiso](https://github.com/XboxDev/extract-xiso) (`extract-xiso -x Black.iso`). Renomeie a pasta extraída para `game`.
2. Verifique se você possui a build suportada. No PowerShell, execute:
   ```powershell
   Get-FileHash game\default.xbe -Algorithm SHA256
   ```
   O resultado DEVE ser um destes:
   - NTSC-U: `DF2739C372D254A90AEECCF5971D12097F22D89FBB90DB021FDA96DD4DEB68F6`
   - PAL Europa: `67F35121CCF622E7A293C402219596FE6456CA4C3D27E6F8139E449264DA30F0`
3. Baixe o arquivo `.zip` na aba [Releases](../../releases) e extraia-o ao lado da pasta `game`, de modo que a pasta `port` fique ao lado dela:
   ```text
   BLACK\
     game\
       default.xbe
       ...
     port\
       black.exe
       blackrt.dll
   ```
4. Execute `port\black.exe`. 

---

## Configuração no Linux (Guia Distrobox + Wine 9 + DXVK)

Para evitar problemas de renderização (como o *crash* nativo do compilador `hlsl.y` em versões antigas do Wine) e garantir o máximo de performance via Vulkan, siga as etapas abaixo.

### Fase 1: Criar o Contêiner Base
O uso do Ubuntu 22.04 isola as dependências de 32 bits sem sujar o seu sistema principal.
1. Abra o terminal nativo da máquina host e crie o contêiner:
   ```bash
   distrobox create --name gaming-env --image ubuntu:22.04
   ```
2. Acesse o interior do contêiner:
   ```bash
   distrobox enter gaming-env
   ```

### Fase 2: Instalar o WineHQ 9+ (Staging)
O Wine 9 moderno elimina bugs crônicos de shaders presentes nas versões legadas.
1. Ative a arquitetura de 32 bits:
   ```bash
   sudo dpkg --add-architecture i386
   ```
2. Baixe as chaves oficiais e adicione o repositório do Ubuntu Jammy:
   ```bash
   sudo mkdir -pm755 /etc/apt/keyrings
   sudo wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key
   sudo wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/ubuntu/dists/jammy/winehq-jammy.sources
   ```
3. Atualize os pacotes e instale o WineHQ Staging:
   ```bash
   sudo apt update
   sudo apt install --install-recommends winehq-staging -y
   ```

### Fase 3: Injetar o DXVK 1.10.3 (Motor Vulkan)
A versão 1.10.3 garante compatibilidade máxima de Vulkan para placas de vídeo integradas (como a linha AMD Vega).
1. Baixe o DXVK e extraia-o no terminal do contêiner:
   ```bash
   wget https://github.com/doitsujin/dxvk/releases/download/v1.10.3/dxvk-1.10.3.tar.gz
   tar -zxf dxvk-1.10.3.tar.gz
   ```
2. Copie as bibliotecas de tradução de 32 bits diretamente para o diretório do jogo (ajuste o caminho de acordo com o seu pc):
   ```bash
   cp dxvk-1.10.3/x32/d3d9.dll /caminho/para/jogo/port/
   cp dxvk-1.10.3/x32/dxgi.dll /caminho/para/jogo/port/
   ```

### Fase 4: Limpeza de Arquivos e Execução
Como arquivos `.zip` criados no Windows geram nomes com barras invertidas literais (`\`) ao serem extraídos no Linux, é necessário limpar a sujeira.
1. Navegue até a pasta do port:
   ```bash
   cd /caminho/para/jogo/port/
   ```
2. Renomeie os arquivos base (removendo o `port\` do nome, se existir):
   ```bash
   mv 'port\black.exe' black.exe
   mv 'port\blackrt.dll' blackrt.dll
   mv 'port\LICENSE.txt' LICENSE.txt
   mv 'port\README.md' README.md
   ```
3. Execute o jogo forçando o Wine a utilizar as bibliotecas nativas do DXVK que copiamos:
   ```bash
   WINEDLLOVERRIDES="d3d9=n;dxgi=n" wine black.exe
   ```

---

## Controles

As teclas e botões do mouse são vinculados diretamente às ações do jogo (não aos botões físicos do controle original) e podem ser alterados em **PC SETTINGS > CONTROLS > KEY BINDINGS**. Controles (gamepads) também funcionam nativamente. 

Clique na janela para capturar o mouse; a tecla **Esc** libera o mouse.

| Ação | Padrão |
|---|---|
| Mover | W A S D |
| Olhar/Mirar | Mouse |
| Atirar / Visada | Botão Esquerdo / Direito do Mouse |
| Recarregar | R |
| Ação (usar, pegar) | E |
| Agachar | C |
| Granada | G |
| Ataque Corpo a Corpo | F |
| Arma Seguinte/Anterior | Roda do mouse (baixo / cima) |
| Trocar modo de disparo | Z |
| Colocar/Tirar Silenciador| Q |
| Kit Médico | X |
| Mostrar Objetivos | Espaço |
| Inclinar à esquerda / direita (experimental) | V / B |
| Pausar | Esc |

## Configurações

Os ajustes do port ficam dentro do menu do jogo: **Options > PC SETTINGS** e no menu de pausa.

| Página | Configurações |
|---|---|
| VIDEO | Resolução, Anti-aliasing, FOV, Brilho, Gama, Tela Cheia |
| DISPLAY | Proporção de tela, Taxa de Quadros, Preenchimento de costuras, Efeitos no tamanho do Xbox |
| TWEAKS | Mira Automática, Desfoque ao recarregar, Ângulo e distância da inclinação |
| AUDIO AND MOVIES | Reverberação de ambiente, Pular qualquer cutscene, Idioma (disco PAL; na próxima inicialização) |
| CONTROLS | Sensibilidade do Mouse, Inversão do Mouse, Mapeamento de Teclas |

Você pode usar argumentos de linha de comando (ex: `black.exe --fullscreen --resolution 1080 --fps 30 --fov 86`).

## Problemas Conhecidos
- O jogo foi feito para 30 fps; a 60 fps (padrão) alguma física pode rodar na velocidade errada.
- Os botões na tela mostram a interface do Xbox, mesmo no teclado.
- Áudio: a reverberação é aproximada e não há canal de subwoofer.
- **Apenas as versões NTSC-U e PAL Europa listadas acima são suportadas.** No disco PAL o idioma do jogo segue o
  Windows (`--language en|de|fr|es|it` ou `language =` no black.ini).

## Compilando o Código-Fonte
Requer Visual Studio 2022 ou 2026 com as ferramentas C++ x86 e CMake 3.24+.
```cmd
cd loader
cmake --preset win32
cmake --build build/win32 --config RelWithDebInfo
```

## Legal e Créditos
Este projeto não é afiliado à Criterion Games ou Electronic Arts. BLACK e Xbox são marcas registradas. Nenhum código original está incluso neste repositório. **Você precisa de uma cópia obtida legalmente do jogo.**
Desenvolvido por Spark198rus. Guia Linux estruturado via IA.
