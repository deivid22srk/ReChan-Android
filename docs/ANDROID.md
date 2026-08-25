# Android port — notas técnicas

## Visão geral

O ReChan desktop usa premake5 + GLFW/SDL2/OpenGL. O upstream já possui um port
para Nintendo Switch (`switch/`) que utiliza o backend GLES3
(`vendor/libp3d/pddi/gles/glesrender.cpp`) com EGL — base do port Android.
O código de jogo (`src/`) é independente de plataforma; os pontos específicos
estão isolados por macros.

## Decisões

| Tema | Escolha |
|---|---|
| Shell | `NativeActivity` (native_app_glue) + `BootActivity` Java para SAF |
| Renderer | GLES3 existente (`pddi/gles`), branch `RC_PLATFORM_ANDROID` |
| Threading | Jogo em thread própria; thread principal bombeia comandos/input |
| Ciclo de vida | Sem surface -> loop pausa em `PollEvents()`; retoma ao voltar |
| Audio | miniaudio (AAudio/OpenSL) — caminho padrão não-Switch |
| ROM | SAF folder picker -> copia p/ `<files>/discimage/game.bin` |
| Assets de suporte | `res/pc/**` embutido no APK + extraído no 1º launch |

## Fluxo do disc image

1. `BootActivity.onCreate`: extrai `pc_manifest.txt` + assets do APK.
2. Se `<files>/discimage/*.{bin,iso}` existe -> inicia jogo.
3. Senão: URI persistida (se houver) ou `ACTION_OPEN_DOCUMENT_TREE`.
4. Procura `*.bin`/`*.iso` na pasta (preferindo slus/jackie/stuntmaster, depois
   maior), copia com progresso para `<files>/discimage/game.bin`, valida tamanho,
   persiste permissão.
5. Extrator ISO9660 do engine roda no 1º boot do jogo (menu nativo).

## Pontes nativas (`vendor/libp3d/pddi/gles/AndroidPlatform.h`)

- `SetNativeWindow/TakePendingWindow/PeekCurrentWindow` — ciclo da janela.
- `RequestExit/ExitRequested` — desligamento limpo.
- `PostGamepadButton/Axis/Connected` + `LoadPadSnapshot` — estado do gamepad.

## Mapeamento de input

- Botões: `BUTTON_A/B/X/Y`, `L1/R1`, `SELECT->Back`, `START/MENU/BACK->Start`,
  `THUMBL/R`, `DPAD_*`; `L2/R2` digitais -> eixos de trigger.
- Sticks: `AXIS_X/Y` (esq.), `Z/RZ` (dir.), Y invertido p/ convenção GLFW.
- Triggers analógicos: `LTRIGGER/BRAKE`, `RTRIGGER/GAS`, 0..1 -> -1..1.
- D-pad analógico: `HAT_X/HAT_Y` com diff de estado (`ApplyHatButtons`).

## Arquivos adicionados/alterados

Adicionados:
- `android/**` — projeto Gradle, manifest, activities Java, shell C++ e CMake.
- `vendor/libp3d/pddi/gles/AndroidPlatform.{h,cpp}` — ponte shell<->renderer.
- `.github/workflows/build.yml` — CI que gera os APKs.

Alterados:
- `vendor/libp3d/pddi/gles/glesrender.{h,cpp}` — branches Android para display
  (EGL + ANativeWindow + pausa/resume) e gamepad; log via logcat.
- `src/gen/main.cpp` — entrypoint vira `GameMain()` no Android.
- `src/gen/config.h` — AUTO_UPDATER desativado no Android.
- `src/pc/crashreporter.cpp` — branch Android (report local + logcat).
