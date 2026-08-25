# ReChan Android — Status do Port

> Estado do trabalho, salvo para retomada entre sessões.
> Última atualização: 2026-08-25

## Metadados

| Item | Valor |
|---|---|
| Upstream | https://github.com/SilverwireGames/ReChan @ `9a45da6448097745c79a1f48ec2f945cf5ca6e30` |
| Repo do port | https://github.com/deivid22srk/ReChan-Android |
| Package | `com.deivid22srk.rechan` |
| ABI alvo | arm64-v8a (device de teste: moto g34 5G, Android 15) |
| minSdk / targetSdk | 24 / 35 |
| ROM necessária? | **Somente em runtime** (não no build). SLUS-00684 NTSC-U `.bin`/`.iso` |
| Entrega da ROM | Usuário seleciona a pasta via SAF (picker) no 1º launch; permissão persistida |
| Controles | Gamepad Bluetooth/USB (prioridade). Touch: não nesta fase |

## Arquitetura descoberta (investigação concluída)

- Build desktop: premake5, C++20. Windows (GLFW+SDL2 vendored) e Linux (X11+SDL2 sistema).
- **Existe um port Nintendo Switch** (`switch/`, `-DRC_PLATFORM_SWITCH -DRC_PLATFORM_NULL -D__SWITCH__`)
  que usa o renderer **GLES3** (`vendor/libp3d/pddi/gles/glesrender.cpp`) via EGL + libnx.
  → Base direta para o port Android: mesmo backend gráfico.
- Pontos específicos de plataforma no código (todos já isolados por `#if defined(RC_PLATFORM_*)`):
  - `vendor/libp3d/pddi/gles/glesrender.cpp`: `nwindowGetDefault()` (janela), `appletMainLoop()`
    (PollEvents), `glesGamepad` com API libnx (`padUpdate` etc.) — **precisam de branch Android**.
  - `src/gen/main.cpp`: entrypoint `main()` + casos RC_PLATFORM_SWITCH.
  - `src/pc/crashreporter.cpp`: branches Windows/Switch/POSIX.
  - `src/pc/audio.cpp`: Switch usa audout libnx; resto usa miniaudio (AAudio/OpenSL no Android).
  - `src/extra/autoupdater.cpp`: shell-out para curl + windows.h — desativar no Android
    (`AUTO_UPDATER` em `src/gen/config.h` é 0 quando `RC_PLATFORM_NULL`; Android terá flag própria).
- File I/O: tudo caminho relativo ao CWD (`std::filesystem` + `fopen`). No Switch fazem `chdir()`.
  No Android: `chdir(internalDataPath)` e extrair assets do APK (`res/pc/**` → `<files>/pc/**`) antes.
- Extração do disco: `PsxDiscExtractor` (portável, ISO9660 MODE2/2352 etc.), roda no 1º launch,
  extrai whitelist (`xc fe rchars rtarget scr sound tim license.tim ...`) para o CWD.
  Sem assets presentes o jogo abre menu próprio (`MenuPage_AssetMissing`) guiando a extração.
- Assets de suporte ReChan (não são do jogo): `res/pc/` (~5 MB, fonts/texturas/textos) —
  vão dentro do APK como assets nativos e são extraídos para `<files>/`.
- ImGui: só usado pelo DebugUI no backend desktop GL; no GLES os overlay callbacks são no-op.
- Áudio: miniaudio 0.11.25 — suporta AAudio/OpenSL|ES sem código extra.

## Decisões tomadas

1. Shell Android = `NativeActivity` (native_app_glue) + Activity bootstrap Kotlin para o SAF picker.
2. Renderer GLES3 existente reutilizado; branch `RC_PLATFORM_ANDROID` nos 3 pontos libnx-specific.
3. Game loop roda em thread própria; thread principal bombeia comandos/input do native_app_glue.
   Pausa (APP_CMD_TERM_WINDOW): loop congela (sem Step/render), EGL surface recriado ao voltar.
4. Disc image via SAF: copiada uma vez para `<files>/discimage/game.bin` (extração precisa de
   path real; evita stream JNI complexo). Permissão persistida; recopia se arquivo mudou.
5. CI: GitHub Actions ubuntu-latest, JDK 17, NDK fixado, `assembleDebug` + `assembleRelease`
   (release assinado com debug key para sideload), artefatos anexados ao run.

## Checklist de progresso

- [x] Investigação do upstream e diagnóstico
- [x] Confirmação dos requisitos de ROM/assets com o usuário
- [x] Estrutura do repositório criada (upstream importado)
- [x] Build Gradle+NDK+CMake compilando todas as fontes
- [x] Shell Android (native_app_glue, ciclo de vida, chdir, extração de assets do APK)
- [x] Branch Android no glesrender (EGL/ANativeWindow/PollEvents/gamepad)
- [x] Input gamepad Android (AInputEvent → pddi)
- [x] Bootstrap Kotlin + SAF folder picker + cópia da ROM
- [x] Crash reporter Android
- [x] Sub-agente crítico: revisão de ponta a ponta (2 rodadas de correção)
- [x] Repo GitHub criado e push realizado
- [x] Workflow build.yml verde no GitHub Actions
- [ ] APK instalado e validado no device via adb

## Rodadas de correção (sub-agentes críticos)

### Rodada 1 — build system (CI)
1. REPO_ROOT do CMake com profundidade errada (4 vs 5 níveis) → build quebrado no CI. **Fix**: `../../../../..`
2. `imgui_demo.cpp` faltando no link (DebugUI referencia ShowDemoWindow). **Fix**: adicionado.
3. Validator do Gradle: lint-vital consumia assets gerados sem dependência. **Fix**: lint `checkReleaseBuilds=false` + dependsOn explícito nos merge*Assets.

### Rodada 2 — runtime (revisões por área)
4. **[CRÍTICO] Extrator de assets do APK não criava diretórios pais** (`pc/fonts` etc.) → 100% dos assets falhariam silenciosamente. **Fix**: `std::filesystem::create_directories` antes do fopen.
5. **[CRÍTICO] Crash SAF**: `takePersistableUriPermission` com flag PERSISTABLE (0x41) → IllegalArgumentException ao selecionar pasta (reproduzido no device, log do usuário). **Fix**: máscara apenas READ|WRITE (0x3).
6. **[CRÍTICO] Surface zumbi no resume**: pending window obsoleto no TERM + ausência de identidade da janela que originou a EGLSurface → tela preta após pause/resume rápido. **Fix**: contador de geração em `androidbridge` + `surfaceGen_` no display + teardown/rebuild por geração; republicação da janela em falha de resume.
7. **[CRÍTICO] Áudio tocava em background** (device miniaudio nunca parava). **Fix**: `AudioEngine::Suspend()/Resume()` enganchados em APP_CMD_PAUSE/RESUME + usage `ma_aaudio_usage_game`.
8. Cópia de ROM não-atômica (morte no meio = game.bin corrompido permanente). **Fix**: `.part` + fsync + rename + limpeza de residuais no boot.
9. ROM trocada na pasta nunca recopiada. **Fix**: comparação name/size via prefs + re-cópia.
10. Sem checagem de espaço em disco antes da cópia (~1,3 GB necessários). **Fix**: getUsableSpace() com mensagem clara.
11. allowBackup=true colocaria a ROM de 628 MB no auto-backup do Google. **Fix**: `allowBackup=false`.
12. HAT do D-pad com estado global único → cross-talk entre 2 controles. **Fix**: estado por deviceId.
13. ANativeWindow acquire sem release (leak). **Fix**: pareamento acquire/release no shell.
14. eglTerminate em caminhos de erro do InitDisplay no Android (display compartilhado). **Fix**: guards `#if !defined(RC_PLATFORM_ANDROID)`.
15. finish() redundante pós-destroy. **Fix**: guarda `destroyRequested`.
16. Cache do Gradle no CI (performance).

## Lições/problemas encontrados (atualizar durante o trabalho)

- `glesDisplay::InitDisplay` precisa esperar a janela existir: android_main aguarda
  APP_CMD_INIT_WINDOW antes de iniciar GameMain.
- `eglCreateWindowSurface` deve receber `(EGLNativeWindowType)ANativeWindow*`.
- D-pad do Android pode chegar como KEYCODE_DPAD_* ou HAT_X/HAT_Y — tratar ambos.
- Triggers analógicos Android variam 0..1; converter para convenção GLFW (-1..1).
- `AKEYCODE_BACK` deve mapear para Start/menu, não para sair do app.
- SAF: `takePersistableUriPermission` aceita APENAS flags READ/WRITE (0x3).
- Sempre copiar arquivos grandes com `.part` + rename (morte no meio = arquivo eternamente corrompido).
- Transições TERM→INIT rápidas entre frames exigem rastreio de geração da janela.
- APP_CMD_PAUSE/RESUME é o par correto para suspender áudio (não TERM/INIT_WINDOW).
