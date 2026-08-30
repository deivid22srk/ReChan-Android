# Changelog — Contextual HUD (feature/contextual-hud)

## Resumo

Overlay de controles touch **contextual** para Android, desacoplado da engine:
o jogo publica um "contexto de gameplay" por frame; a camada Java consome via
JNI e adapta os controles em tela. Gamepad físico conectado → HUD some;
desconectado → volta, sem pausar o jogo.

## Novos arquivos

| Arquivo | Papel |
|---|---|
| `src/extra/touchhud.{h,cpp}` | Publica o contexto (Hidden/OnFoot/Climbing) por frame + funções JNI (input touch → estado de gamepad compartilhado) |
| `android/.../hud/HudBridge.java` | Contrato JNI (botões/eixos pddi + contexto) |
| `android/.../hud/HudController.java` | Detecção de gamepad físico (hot-plug), polling de contexto (100 ms), persistência do layout |
| `android/.../hud/TouchHudView.java` | View do overlay: joystick virtual + botões, multi-touch, fades, modo edição, safe-areas, haptics |
| `docs/TOUCH_HUD_TESTS.md` | Matriz de testes manuais |

## Alterações

- `src/gen/main.cpp`: chama `touchhud::PublishFrame()` por frame (Android only).
- `vendor/libp3d/pddi/gles/AndroidPlatform.{h,cpp}`: atomics `SetHudContext/LoadHudContext`.
- `android/.../GameActivity.java`: anexa/desanexa o `HudController`.
- `android/app/build.gradle.kts`: keystore estável commitado (`rechan.keystore`)
  para debug **e** release — APKs de qualquer run do CI agora se atualizam
  in-place (fim dos conflitos `INSTALL_FAILED_UPDATE_INCOMPATIBLE`).
- `.github/workflows/build.yml`: roda também em `feature/**`.
- Fix incluído da main: analógico Y invertido (Android AXIS_Y já é convenção GLFW).

## Decisões técnicas

1. **Overlay Java (View) em vez de renderizar na engine**: desacopla HUD do
   GLES state do jogo; input touch entra pelo mesmo caminho de um gamepad
   físico (`androidbridge`), impossibilitando input duplicado por construção.
2. **Pull model para contexto** (Java polla `nativePollContext()` a cada
   100 ms): evita plumbing de JavaVM/AttachCurrentThread; latência mascarada
   pelos fades de 180 ms.
3. **Contextos v1**: `Hidden` (menu/cutscene/NIS/estados não-Play),
   `OnFoot`, `Climbing` (actionStates 18, 23–28: pole/ledge/ladder).
   Sinais usados: `GameState`, `g_feCustomMenuMgr->IsActive()`,
   `g_directorActive`/`g_director->scriptState`, `Player::s_player->actionState`.
4. **Gamepad real ≠ falso positivo**: exige `SOURCE_GAMEPAD` ou
   (`SOURCE_JOYSTICK` + eixos joystick em `getMotionRanges()`); remotes DPAD-only,
   mouse e teclado não ocultam o HUD. Multi-device: re-scan no remove
   (esconde só quando o ÚLTIMO gamepad cai).
5. **Persistência JSON** em SharedPreferences (posições normalizadas →
   independente de resolução), opacidade e modo contorno globais.
6. **Keystore commitado**: projeto de sideload; assinatura estável > segredo
   (não é um limite de segurança real para distribuição por fora da Play).

## Limitações conhecidas (v1)

- Sem detecção de "perto de interativo": botão B (grab) é sempre exibido no
  contexto OnFoot (não troca de ícone perto de veículo/objeto — o jogo não
  expõe um sinal de proximidade pronto).
- Sem "veículo": JCS não tem direção livre de veículos; contextos de carrinho/
  skate são cutscenes/estados scriptados (cobertos por Hidden/OnFoot).
- Modo edição: arraste de um dedo; sem pinch-zoom (tamanho via chips).
- HUD não aparece em API < 30 com safe-areas (funciona, sem ajuste de notch).
- Transições de contexto têm latência de até 100 ms + 180 ms de fade.

## Migração de instalação

A troca de assinatura (keystore estável) exige **desinstalar o app antigo uma
última vez**. A ROM copiada (`discimage/game.bin`) será apagada junto — no
próximo boot, selecione a pasta da ROM novamente (a cópia é automática).
