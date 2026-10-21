# Guia de Controles e Lista de Keybinds (BLACK PC)

Este documento contém a referência completa de controles e teclas de atalho (*keybinds*) para o port de **BLACK** (Windows e Linux).

O jogo utiliza um sistema desacoplado onde as ações do jogador são vinculadas dinamicamente à tabela de canais do Xbox (`0x002F3BD4`). Isso permite personalizar até **duas teclas por ação** (via teclado/mouse) sem interferir no suporte nativo a controles/gamepads.

---

## 1. Ações de Jogabilidade (Gameplay)

| Ação | Nome no Jogo | Tecla Padrão 1 | Tecla Padrão 2 | Configuração no `black.ini` |
|---|---|---|---|---|
| **Mover para Frente** | Run forward | `W` | *(nenhuma)* | `key_run_forward = W` |
| **Mover para Trás** | Run back | `S` | *(nenhuma)* | `key_run_back = S` |
| **Passo Lateral Esquerdo** | Sidestep left | `A` | *(nenhuma)* | `key_step_left = A` |
| **Passo Lateral Direito** | Sidestep right | `D` | *(nenhuma)* | `key_step_right = D` |
| **Olhar / Mirar (Câmera)** | Look | `Movimento do Mouse` | *(nenhuma)* | `mouse_sens = 2.0` |
| **Disparar (Tiro)** | Fire | `Mouse1` (Botão Esquerdo) | *(nenhuma)* | `key_fire = Mouse1` |
| **Mirar (Mira de Ferro / Luneta)** | Aim | `Mouse2` (Botão Direito) | *(nenhuma)* | `key_aim = Mouse2` |
| **Recarregar** | Reload | `R` | *(nenhuma)* | `key_reload = R` |
| **Ação (Usar / Pegar Arma)** | Action (use, pick up) | `E` | *(nenhuma)* | `key_use = E` |
| **Agachar** | Crouch | `C` | *(nenhuma)* | `key_crouch = C` |
| **Lançar Granada** | Grenade | `G` | *(nenhuma)* | `key_grenade = G` |
| **Coronhada (Ataque Melee)** | Melee | `F` | *(nenhuma)* | `key_melee = F` |
| **Próxima Arma** | Next weapon | `WheelDown` (Roda p/ Baixo) | *(nenhuma)* | `key_next_weapon = WheelDown` |
| **Arma Anterior** | Previous weapon | `WheelUp` (Roda p/ Cima) | *(nenhuma)* | `key_prev_weapon = WheelUp` |
| **Alternar Modo de Disparo** | Change firing mode | `Z` | *(nenhuma)* | `key_firing_mode = Z` |
| **Alternar Silenciador (Supressor)** | Toggle suppressor | `Q` | *(nenhuma)* | `key_suppressor = Q` |
| **Usar Kit Médico (Vida)** | Health pack | `X` | *(nenhuma)* | `key_health_pack = X` |
| **Exibir Objetivos** | Show objectives | `Espaço` (Space) | *(nenhuma)* | `key_objectives = Space` |
| **Pausar Jogo** | Pause | `Esc` (Escape) | *(nenhuma)* | `key_pause = Esc` |
| **Próxima Faixa de Música** | Next music track | *(não atribuída)* | *(nenhuma)* | `key_music = none` |
| **Girar para Esquerda** | Turn left | *(não atribuída)* | *(nenhuma)* | `key_turn_left = none` |
| **Girar para Direita** | Turn right | *(não atribuída)* | *(nenhuma)* | `key_turn_right = none` |
| **Olhar para Cima** | Look up | *(não atribuída)* | *(nenhuma)* | `key_look_up = none` |
| **Olhar para Baixo** | Look down | *(não atribuída)* | *(nenhuma)* | `key_look_down = none` |

> [!NOTE]
> As ações de girar (*Turn*) e olhar (*Look up/down*) via teclado são opcionais e desligadas por padrão, pois o controle analógico da câmera é feito com alta precisão diretamente pelo mouse.

---

## 2. Menus e Navegação de Interface

Nos menus, pausas e cutscenes, as teclas de ação do jogo não operam diretamente. A interface possui controles dedicados:

| Comando | Tecla / Ação | Função |
|---|---|---|
| **Navegar para Cima** | `Seta Acima` (Up Arrow) | Move o cursor para o item anterior/acima |
| **Navegar para Baixo** | `Seta Abaixo` (Down Arrow) | Move o cursor para o item posterior/abaixo |
| **Ajustar Esquerda** | `Seta Esquerda` (Left Arrow) | Reduz valor do slider ou altera opção à esquerda |
| **Ajustar Direita** | `Seta Direita` (Right Arrow) | Aumenta valor do slider ou altera opção à direita |
| **Selecionar / Confirmar** | `Enter` | Confirma seleção ou entra em submenus |
| **Voltar / Cancelar** | `Backspace` | Retorna à tela anterior ou cancela |
| **Iniciar Jogo ("PRESS START")** | `Enter` | Ativa a porta 0 e inicia o jogo |
| **Capturar Cursor do Mouse** | `Clique com Botão Esquerdo` | Prende o mouse à janela do jogo e ativa a mira |
| **Liberar Cursor do Mouse** | `Esc` ou `Alt + Tab` | Pausa a jogabilidade e devolve o cursor ao desktop |

---

## 3. Pulo de Cutscenes e Vídeos

| Comando | Teclas Aceitas | Função |
|---|---|---|
| **Pular Filme / Cutscene FMV** | `Enter`, `Espaço` ou `Esc` | Pula vídeos pré-renderizados de introdução e história |
| **Pular Texto de Início de Missão** | `Enter` ou `Gamepad START` | Pula a introdução datilografada da missão |
| **Fechar Janela de Dica Tática** | `Enter` ou `Gamepad A` | Fecha avisos de *"PRESS A TO CONTINUE"* no HUD |

*(Dica: O pulo de vídeos pode ser forçado ou bloqueado pelas flags `--skip-movies` ou `--no-movie-skip` na linha de comando).*

---

## 4. Gamepad / Controle Físico (Xbox / SDL2)

Qualquer controle compatível com SDL2 (Xbox 360, Xbox One, Series X/S, DualShock 4, DualSense, etc.) mapeia automaticamente para a porta 0 do console Xbox:

| Controle Xbox | Botão / Eixo SDL2 | Ação no Jogo |
|---|---|---|
| **Analógico Esquerdo (LS)** | `SDL_CONTROLLER_AXIS_LEFTX / LEFTY` | Movimentação do personagem (*Run / Strafe*) |
| **Analógico Direito (RS)** | `SDL_CONTROLLER_AXIS_RIGHTX / RIGHTY` | Rotação de câmera (*Look / Aim*) |
| **Gatilho Esquerdo (LT)** | `SDL_CONTROLLER_AXIS_TRIGGERLEFT` | Mirar (*Aim*) |
| **Gatilho Direito (RT)** | `SDL_CONTROLLER_AXIS_TRIGGERRIGHT` | Atirar (*Fire*) |
| **Botão A** | `SDL_CONTROLLER_BUTTON_A` | Exibir Objetivos / Selecionar no Menu / Confirmar Dica |
| **Botão B** | `SDL_CONTROLLER_BUTTON_B` | Agachar (*Crouch*) / Voltar no Menu |
| **Botão X** | `SDL_CONTROLLER_BUTTON_X` | Recarregar (*Reload*) |
| **Botão Y** | `SDL_CONTROLLER_BUTTON_Y` | Ação (*Use / Pick up*) / Trocar de Arma |
| **Ombro Direito (RB / Black)** | `SDL_CONTROLLER_BUTTON_RIGHTSHOULDER` | Lançar Granada (*Grenade*) |
| **Ombro Esquerdo (LB / White)** | `SDL_CONTROLLER_BUTTON_LEFTSHOULDER` | Silenciador (*Suppressor*) |
| **D-Pad Esquerda / Direita** | `SDL_CONTROLLER_BUTTON_DPAD_LEFT / RIGHT` | Trocar Arma / Modo de Disparo |
| **D-Pad Cima / Baixo** | `SDL_CONTROLLER_BUTTON_DPAD_UP / DOWN` | Usar Kit Médico (*Health Pack*) |
| **Clique Analógico Direito (RS Click / R3)** | `SDL_CONTROLLER_BUTTON_RIGHTSTICK` | Ataque Coronhada (*Melee*) |
| **START** | `SDL_CONTROLLER_BUTTON_START` | Menu de Pausa / Pular introduções |
| **BACK** | `SDL_CONTROLLER_BUTTON_BACK` | Objetivos / Cancelar |

---

## 5. Como Customizar os Keybinds

### Método 1: Pelo Menu do Jogo (Recomendado)
1. No menu principal ou durante a pausa (`Esc`), acesse:
   **Options > PC SETTINGS > CONTROLS > KEY BINDINGS**.
2. Navegue com as setas até a ação desejada e pressione `Enter` na coluna da tecla 1 ou tecla 2.
3. Pressione a nova tecla ou botão do mouse desejado.
4. Pressione `Esc` para cancelar a captura ou `Delete` para limpar a tecla atribuída.
5. As alterações são salvas automaticamente no arquivo `black.ini`.

### Método 2: Manualmente via Arquivo `black.ini`
Você pode editar diretamente o arquivo [`black.ini`](file:///home/nubi/Programacao/black-pc/black.ini) na raiz do jogo:
```ini
key_run_forward = W
key_run_back = S
key_step_left = A
key_step_right = D
key_fire = Mouse1
key_aim = Mouse2
key_crouch = C,LCtrl
```
*(Para configurar duas teclas na mesma ação, separe por vírgula: `key_crouch = C,LCtrl`).*

#### Nomes de Teclas Reconhecidos no `black.ini`:
- **Mouse:** `Mouse1` (Esquerdo), `Mouse2` (Direito), `Mouse3` (Meio), `Mouse4`, `Mouse5`, `WheelUp` (Roda cima), `WheelDown` (Roda baixo).
- **Teclado Básico:** `A`-`Z`, `0`-`9`, `Space`, `Enter`, `Esc`, `Tab`, `Backspace`.
- **Modificadores:** `Shift`, `Ctrl`, `Alt`, `LShift`, `RShift`, `LCtrl`, `RCtrl`, `LAlt`, `RAlt`, `CapsLock`.
- **Navegação & Edição:** `Up`, `Down`, `Left`, `Right`, `Insert`, `Delete`, `Home`, `End`, `PageUp`, `PageDown`.
- **Teclado Numérico:** `Num0`-`Num9`, `Num*`, `Num+`, `Num-`, `Num.`, `Num/`, `NumLock`.
- **Teclas de Função:** `F1` a `F24`.
- **Outras / Hexadecimal:** `Pause`, `ScrollLock`, `Menu`, ou código hexadecimal direto `0xNN`.

---

## 6. Comandos e Opções Úteis de Linha de Comando

```bash
# Exibir lista de keybinds ativos diretamente no terminal / prompt
./black_linux --keybinds    # Linux
black.exe --keybinds        # Windows

# Exibir ajuda completa de opções CLI
./black_linux --help

# Forçar exibição do HUD Vulkan com contador de FPS e tempo de quadro
./black_linux --hud

# Executar com resolução interna 720p em modo tela cheia
./black_linux --resolution 1280x720 --fullscreen
```
