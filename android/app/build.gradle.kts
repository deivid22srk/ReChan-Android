plugins {
    id("com.android.application")
}

// ReChan-authored support files (fonts/textures/text) live at the repo root in
// res/pc. They are synced into the APK assets and extracted on-device on first
// launch; pc_manifest.txt lists every file for the native extractor.
val gameAssetsOutDir = layout.buildDirectory.dir("generated/gameAssets")

val syncGameSupportAssets = tasks.register<Sync>("syncGameSupportAssets") {
    val srcDir = rootProject.file("../res/pc")
    from(srcDir) {
        into("pc")
    }
    into(gameAssetsOutDir)
    doLast {
        val outDir = gameAssetsOutDir.get().asFile
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

    // Stable sideload signing: the keystore is committed so APKs from any
    // CI run (and local builds) share one signature and update in place.
    signingConfigs {
        create("sideload") {
            storeFile = file("rechan.keystore")
            storePassword = "rechan"
            keyAlias = "rechan"
            keyPassword = "rechan"
        }
    }

    defaultConfig {
        applicationId = "com.deivid22srk.rechan"
        minSdk = 24
        targetSdk = 35
        versionCode = 2
        versionName = "1.0.2"

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
            signingConfig = signingConfigs.getByName("sideload")
        }
        release {
            isMinifyEnabled = false
            // Sideload distribution: same stable key for both build types.
            signingConfig = signingConfigs.getByName("sideload")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(layout.buildDirectory.dir("generated/gameAssets"))
        }
    }

    lint {
        // Sideload distribution; the lint-vital release gate adds no value here.
        checkReleaseBuilds = false
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
