plugins {
    id("com.android.application")
}

// ReChan-authored support files (fonts/textures/text) live at the repo root in
// res/pc. They are synced into the APK assets and extracted on-device on first
// launch; pc_manifest.txt lists every file for the native extractor.
val gameAssetsDir = layout.buildDirectory.dir("generated/gameAssets")

val syncGameSupportAssets = tasks.register("syncGameSupportAssets") {
    val srcDir = rootProject.file("../res/pc")
    val outDir = gameAssetsDir.get().asFile
    inputs.dir(srcDir)
    outputs.dir(outDir)
    doLast {
        copy {
            from(srcDir)
            into(File(outDir, "pc"))
        }
        val paths = sortedSetOf<String>()
        srcDir.walkTopDown().filter { it.isFile }.forEach { f ->
            paths.add("pc/" + f.relativeTo(srcDir).invariantSeparatorsPath)
        }
        File(outDir, "pc_manifest.txt").writeText(paths.joinToString("\n") + "\n")
    }
}

android {
    namespace = "com.deivid22srk.rechan"
    compileSdk = 35
    // Matches AGP 8.9's default and the preinstalled default on GitHub runners.
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.deivid22srk.rechan"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.1"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
            // Sideload distribution: sign releases with the debug key so the
            // APK installs without extra setup.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(gameAssetsDir)
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }
}

tasks.matching { it.name.startsWith("merge") && it.name.contains("Assets") }.configureEach {
    dependsOn(syncGameSupportAssets)
}

tasks.matching { it.name.startsWith("generateJsonConfig") ||
                 it.name.startsWith("configureCMake") ||
                 it.name.startsWith("buildCMake") }.configureEach {
    dependsOn(syncGameSupportAssets)
}
