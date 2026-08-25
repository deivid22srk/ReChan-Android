# Contextual HUD — Plano de Testes Manuais

Feature: `feature/contextual-hud` — overlay touch contextual com auto-hide
por gamepad físico.

## Setup comum

1. Instalar APK `rechan-debug-arm64` (ou release) do run da branch.
2. Iniciar o jogo e aguardar a gameplay (pular intro, confirmar assets).

## 1. Contextos de gameplay (troca em tempo real)

| # | Cenário | Esperado |
|---|---------|----------|
| 1.1 | Jogo em gameplay normal (a pé) | HUD visível: joystick + A/B/X/Y/R1/L1/Start |
| 1.2 | Abrir o menu (Start ou BACK) | HUD some com fade (~180 ms); botões virtuais soltos (sem input preso) |
| 1.3 | Fechar o menu | HUD volta com fade+scale |
| 1.4 | Cutscene (NIS/director) iniciar | HUD some |
| 1.5 | Cutscene terminar | HUD volta |
| 1.6 | Subir escada/ledge/pole (actionState 18,23–28) | HUD reduzido: só joystick + A |
| 1.7 | Sair da escada | HUD completo volta |
| 1.8 | Tela de título/intro/load | HUD oculto |
| 1.9 | Combate (soco/chute) | HUD completo permanece (contexto OnFoot) |

## 2. Gamepad físico — show/hide automático

| # | Cenário | Esperado |
|---|---------|----------|
| 2.1 | Conectar gamepad Bluetooth DURANTE gameplay | HUD some automaticamente, sem pausar o jogo; controles físicos funcionam |
| 2.2 | Desconectar gamepad Bluetooth | HUD reaparece automaticamente |
| 2.3 | Conectar gamepad USB/OTG durante gameplay | Igual 2.1 |
| 2.4 | Desconectar USB | Igual 2.2 |
| 2.5 | App já aberto com gamepad conectado desde antes | HUD nunca aparece |
| 2.6 | Dois gamepads: desconectar UM | HUD permanece oculto (ainda há 1 conectado) |
| 2.7 | Dois gamepads: desconectar AMBOS | HUD reaparece |
| 2.8 | Mouse USB conectado | HUD NÃO some (mouse não é gamepad) |
| 2.9 | Controle remoto de TV (DPAD-only) | HUD NÃO some (sem eixo joystick / sem SOURCE_GAMEPAD) |
| 2.10 | Teclado Bluetooth | HUD NÃO some |

Logs de depuração (debug build): `adb shell setprop log.tag.rechan-hud DEBUG`
antes de abrir o app; eventos aparecem em `adb logcat -s rechan-hud`.

## 3. Touch input

| # | Cenário | Esperado |
|---|---------|----------|
| 3.1 | Joystick: movimentos suaves em todas as direções | Personagem anda/corre proporcionalmente; cima = cima |
| 3.2 | Multi-touch: andar + socar simultâneos | Ambos funcionam |
| 3.3 | Botão A (pular) durante corrida | Salto responde |
| 3.4 | Menu via ≡ (Start) | Menu abre; navegar por D-pad do gamepad se conectado |
| 3.5 | Tocar fora dos controles | Nada acontece (sem input fantasma no jogo) |
| 3.6 | Segurar botão e abrir menu (contexto muda) | Botão é liberado (releaseAll) — sem input preso |

## 4. Layout/edição/persistência

| # | Cenário | Esperado |
|---|---------|----------|
| 4.1 | Long-press (600 ms) em um controle | Entra em modo edição; controle selecionado com anel âmbar; chips no topo |
| 4.2 | Arrastar controle selecionado | Move; não envia input ao jogo |
| 4.3 | Chips "Menor"/"Maior" | Redimensiona o controle selecionado |
| 4.4 | Chips "Opac-"/"Opac+" | Ajusta opacidade global (5% por toque) |
| 4.5 | Chip "Contorno" | Alterna modo baixo contraste (contorno vazado) |
| 4.6 | Chip "Concluir" | Sai e salva; fechar/reabrir o app restaura tudo |
| 4.7 | Matar o app em edição | Sem corrupção (última configuração salva prevalece) |

## 5. Telas/orientação

| # | Cenário | Esperado |
|---|---------|----------|
| 5.1 | Tela 16:9 (720x1600 landscape) | Controles dentro da área segura, sem sobrepôr notch |
| 5.2 | Tela 20:9/21:9 | Joystick e cluster de ações proporcionais, alcançáveis |
| 5.3 | Rotação (se permitida pelo sistema) | Layout reproporcional (posições normalizadas) |

## 6. Regressão dos controles existentes

| # | Cenário | Esperado |
|---|---------|----------|
| 6.1 | Gamepad físico: D-pad | Funciona como antes (sem interferência do HUD) |
| 6.2 | Gamepad físico: analógico | Direção correta (cima=cima) — inclui fix do eixo Y |
| 6.3 | Gamepad físico: botões A/B/X/Y/L/R/Start | Mapeamento inalterado |
| 6.4 | BACK/MENU físicos → Start do jogo | Inalterado |
