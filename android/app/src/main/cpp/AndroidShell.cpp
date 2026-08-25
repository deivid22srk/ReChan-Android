// AndroidShell.cpp — NativeActivity glue: lifecycle, filesystem setup, game
// thread. The game loop itself (src/gen/main.cpp GameMain) runs on a dedicated
// thread; this thread pumps the android_app command/input queues until the
// activity is destroyed.
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <filesystem>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "pddi/gles/AndroidPlatform.h"

extern int GameMain(int argc, char** argv);
bool RechanAndroidHandleInputEvent(AInputEvent* event);

namespace {

android_app* g_app = nullptr;
pthread_t g_gameThread = {};
bool g_gameThreadStarted = false;

struct AssetTask {
    std::string assetPath; // path inside the APK assets/
    std::string outPath;   // absolute output file
};

void ExtractManifestAssets(AAssetManager* mgr) {
    AAsset* manifest = AAssetManager_open(mgr, "pc_manifest.txt", AASSET_MODE_BUFFER);
    if (!manifest) {
        __android_log_print(ANDROID_LOG_ERROR, "rechan",
                            "pc_manifest.txt missing from APK assets");
        return;
    }
    const off_t len = AAsset_getLength64(manifest);
    const char* begin = static_cast<const char*>(AAsset_getBuffer(manifest));
    std::vector<char> lines(begin, begin + len);
    AAsset_close(manifest);

    std::string line;
    size_t pos = 0;
    while (pos <= lines.size()) {
        if (pos == lines.size() || lines[pos] == '\n') {
            // flush current line
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            if (!line.empty()) {
                AAsset* asset =
                    AAssetManager_open(mgr, line.c_str(), AASSET_MODE_STREAMING);
                if (!asset) {
                    __android_log_print(ANDROID_LOG_WARN, "rechan",
                                        "asset missing in APK: %s", line.c_str());
                    line.clear();
                    ++pos;
                    continue;
                }
                const std::string outPath = line; // relative to CWD (<files>)
                // Skip when already extracted with identical size.
                FILE* existing = fopen(outPath.c_str(), "rb");
                if (existing) {
                    fseek(existing, 0, SEEK_END);
                    const long sz = ftell(existing);
                    fclose(existing);
                    if (sz == static_cast<long>(AAsset_getLength64(asset))) {
                        AAsset_close(asset);
                        line.clear();
                        ++pos;
                        continue;
                    }
                }

                const std::filesystem::path parent =
                    std::filesystem::path(outPath).parent_path();
                if (!parent.empty()) {
                    std::filesystem::create_directories(parent);
                }
                FILE* out = fopen(outPath.c_str(), "wb");
                if (!out) {
                    __android_log_print(ANDROID_LOG_ERROR, "rechan",
                                        "cannot write %s", outPath.c_str());
                    AAsset_close(asset);
                    line.clear();
                    ++pos;
                    continue;
                }
                char buf[65536];
                int read;
                while ((read = AAsset_read(asset, buf, sizeof(buf))) > 0) {
                    fwrite(buf, 1, static_cast<size_t>(read), out);
                }
                fclose(out);
                AAsset_close(asset);
            }
            line.clear();
            ++pos;
        }
        else {
            line.push_back(lines[pos]);
            ++pos;
        }
    }
}

void PrepareFileSystem(android_app* app) {
    const char* filesDir = app->activity->internalDataPath;
    if (!filesDir || chdir(filesDir) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "rechan",
                            "chdir to internalDataPath failed (%s)",
                            filesDir ? filesDir : "<null>");
    }
    mkdir("discimage", 0755);
    mkdir("userfiles", 0755);
    mkdir("~mods", 0755);
    mkdir("minidumps", 0755);
    ExtractManifestAssets(app->activity->assetManager);
    __android_log_print(ANDROID_LOG_INFO, "rechan", "CWD=%s", getcwd(nullptr, 0));
}

void HandleCommand(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                ANativeWindow_acquire(app->window);
                androidbridge::SetNativeWindow(app->window);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            androidbridge::SetNativeWindow(nullptr);
            break;
        case APP_CMD_DESTROY:
            androidbridge::RequestExit();
            break;
        default:
            break;
    }
}

int HandleInput(android_app*, AInputEvent* event) {
    return RechanAndroidHandleInputEvent(event) ? 1 : 0;
}

void PumpEventsOnce(android_app* app) {
    int events = 0;
    android_poll_source* source = nullptr;
    const int ident =
        ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source));
    if (ident >= 0 && source) {
        source->process(app, source);
    }
}

void PumpEventsUntilWindowOrDestroy(android_app* app) {
    while (!app->destroyRequested && !androidbridge::PeekCurrentWindow()) {
        PumpEventsOnce(app);
    }
}

void* GameThreadFunc(void*) {
    char argv0[] = "rechan";
    char* argv[] = {argv0, nullptr};
    GameMain(1, argv);

    // The engine loop ended (in-game quit or exit request): close the app.
    ANativeActivity_finish(g_app->activity);
    return nullptr;
}

void StartGameThread() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4 * 1024 * 1024); // deep recursion in FE code
    g_gameThreadStarted = true;
    pthread_create(&g_gameThread, &attr, GameThreadFunc, nullptr);
    pthread_attr_destroy(&attr);
}

} // namespace

void android_main(android_app* state) {
    g_app = state;
    state->onAppCmd = HandleCommand;
    state->onInputEvent = HandleInput;

    PrepareFileSystem(state);

    // Wait for a window (or destruction) before touching EGL.
    PumpEventsUntilWindowOrDestroy(state);

    if (!state->destroyRequested && androidbridge::PeekCurrentWindow()) {
        StartGameThread();
    }

    // Keep servicing the OS queues for as long as we live.
    while (!state->destroyRequested) {
        PumpEventsOnce(state);
    }

    if (g_gameThreadStarted) {
        pthread_join(g_gameThread, nullptr);
    }
}
