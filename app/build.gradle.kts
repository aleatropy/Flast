import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Loaded from keystore/keystore.properties, which is gitignored and NOT
// part of this repo/zip -- copy keystore/keystore.properties.example to
// keystore/keystore.properties and fill in the real passwords before
// building a signed release. Falls back to null (unsigned release, same
// as before) if that file isn't present, so debug builds and CI checkouts
// without the secret file still work.
val keystorePropertiesFile = rootProject.file("keystore/keystore.properties")
val keystoreProperties = Properties().apply {
    if (keystorePropertiesFile.exists()) {
        keystorePropertiesFile.inputStream().use { load(it) }
    }
}

android {

    ndkVersion = "27.0.12077973"

    namespace = "com.flast"

    compileSdk = 35

    defaultConfig {
        applicationId = "com.flast"
        // Android 8.0. This is AAudio's own floor, not a preference:
        // AAudio_createStreamBuilder and friends do not exist below API
        // 26, and reaching further back would mean a second OpenSL ES
        // audio backend that can never be bit-perfect -- a different
        // product, not a port. Verified by compiling the whole native
        // layer with -Werror=unguarded-availability at 21/23/26/29/30/31:
        // clean from 26 up, fails at 23 and below on AAudio symbols.
        //
        // This replaces the API 31 floor of spec section 2, whose stated
        // justification was AAudio EXCLUSIVE *maturity* rather than
        // availability. That argument was written when an immature HAL
        // meant the app played nothing at all; it now falls back to
        // SHARED and reports BIT-PERFECT: NO, so the reason for excluding
        // Android 8-11 no longer holds. See FLAC_PLAYER_SPEC_v2.md 2.1.
        minSdk = 26
        targetSdk = 35
        // First PUBLIC release. Everything before this was internal
        // iteration that never left this machine, so the public history
        // starts here at 1 rather than carrying private build numbers
        // into a repository nobody has seen yet.
        versionCode = 1
        versionName = "1.0.0"

        externalNativeBuild {
            cmake {
                // Every ABI Android still ships on a phone/tablet in
                // 2026. This used to be arm64-v8a ONLY, which is the
                // single biggest reason the app "doesn't open on other
                // devices": a 32-bit-only handset cannot install an APK
                // with no armeabi-v7a library at all (the installer
                // fails with INSTALL_FAILED_NO_MATCHING_ABIS), and the
                // x86_64 emulator is in the same boat. `splits.abi`
                // below means adding them costs a device NOTHING -- each
                // one still downloads a single-ABI APK the same size as
                // before, and armeabi-v7a is actually the smallest of
                // the three.
                abiFilters += listOf("armeabi-v7a", "arm64-v8a", "x86_64")
            }
        }
    }

    // One APK per ABI instead of one fat APK carrying three copies of
    // the native library. Distribution is GitHub + F-Droid (spec
    // section 8), both of which serve per-ABI APKs natively, so the
    // universality fix above does not cost the user a single byte.
    // If this ever goes to Play, build an AAB instead (`bundleRelease`)
    // -- Play does this same split server-side and wants monotonically
    // increasing versionCodes per split, which an AAB handles for you.
    splits {
        abi {
            isEnable = true
            reset()
            include("armeabi-v7a", "arm64-v8a", "x86_64")
            // Also emit one APK containing all three ABIs. The per-ABI
            // APKs stay exactly as they were -- this is an ADDITIONAL
            // artifact for people who do not know (and should not have
            // to know) what CPU their phone has. At ~250 KB it is still
            // smaller than the icon of most music players, so "when in
            // doubt, download this one" costs the user nothing real.
            isUniversalApk = true
        }
    }

    if (keystorePropertiesFile.exists()) {
        signingConfigs {
            create("release") {
                storeFile = rootProject.file("keystore/${keystoreProperties["storeFile"]}")
                storePassword = keystoreProperties["storePassword"] as String
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
                // v2 is what every supported device (API 26+) verifies with,
                // and v1/JAR signing is correctly off because nothing below
                // API 24 can install this. v3 is enabled because it carries
                // the proof-of-rotation block: without it in the FIRST
                // published release, this project could never rotate its
                // signing key later without breaking every existing install.
                enableV1Signing = false
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    // Nothing here is used by the app, and each one costs either APK
    // bytes or a generated class in classes.dex:
    //   buildConfig  -> generates com.flast.BuildConfig, never read
    //   resValues    -> no resValue() declarations exist
    //   aidl/shaders/renderScript -> no such sources exist
    buildFeatures {
        buildConfig = false
        resValues = false
        aidl = false
        shaders = false
        renderScript = false
    }

    // AGP normally embeds a signed blob listing every library the app
    // was built against into the APK signing block. This app has zero
    // dependencies, so the blob describes nothing -- and the project's
    // transparency stance (spec section 6) is better served by an APK
    // that contains only what the source tree explains.
    dependenciesInfo {
        includeInApk = false
        includeInBundle = false
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
            if (keystorePropertiesFile.exists()) {
                signingConfig = signingConfigs.getByName("release")
            }
            // Drops the res/version-control-info.textproto entry AGP
            // otherwise stamps into every release APK.
            vcsInfo {
                include = false
            }
            // Explicit Release C/C++ build type: without this the NDK
            // toolchain has no optimization level set for
            // flacplayer_native (native_bridge.c, flac_stream.c,
            // playback_controller.c, music_library.c, ui_main.c,
            // ui_render.c) and builds it effectively at -O0 with debug
            // info even for a "release" APK. The size flags themselves
            // (-Os, --gc-sections, hidden visibility, no unwind tables)
            // live in src/main/cpp/CMakeLists.txt so they apply to
            // every target including libFLAC.
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=Release"
                }
            }
            // Strip native debug symbols from the release .so instead of
            // shipping them -- they're dead weight in an APK the user
            // isn't going to attach a debugger to, and typically account
            // for a large chunk of a native library's on-disk size.
            ndk {
                debugSymbolLevel = "none"
            }
        }
        debug {

            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
        // Kotlin emits an Intrinsics.checkNotNullParameter() call at the
        // top of every function with a non-null parameter, plus
        // checkNotNullExpressionValue() after platform-type calls. They
        // exist to turn a null coming from *Java* callers into a clean
        // exception -- and nothing in this app is called from Java: the
        // entry points are the framework (which never passes null where
        // the signatures say non-null) and JNI, which is checked by
        // hand in native_bridge.c. Dropping them removes a call+string
        // constant from every such function in classes.dex.
        freeCompilerArgs += listOf(
            "-Xno-param-assertions",
            "-Xno-call-assertions",
            "-Xno-receiver-assertions",
        )
    }

    packaging {
        resources {
            // No native tests, docs, or license fragments from the FLAC
            // third_party tree should ever end up in the APK (CMakeLists
            // only compiles the small decoder-only subset already), but
            // this belt-and-suspenders exclude keeps any stray non-code
            // assets some tool might pick up out of the final package.
            excludes += setOf("META-INF/*.md", "META-INF/LICENSE*", "META-INF/NOTICE*")

            // ~30KB of pure dead weight measured in the v1.0.0 APK.
            // kotlin-stdlib ships its declaration metadata as loose
            // resources (kotlin/kotlin.kotlin_builtins alone was 18.6KB,
            // and there were six more) so that *reflection* can
            // reconstruct built-in types at runtime. This app has no
            // kotlin-reflect, calls no ::class.members, and does no
            // serialization -- nothing reads these files, they are just
            // copied into the APK because they sit on the runtime
            // classpath. kotlin-tooling-metadata.json is IDE/tooling
            // information about the Kotlin build, likewise never read at
            // runtime.
            excludes += setOf(
                "kotlin/**",
                "**/*.kotlin_builtins",
                "**/*.kotlin_metadata",
                "META-INF/*.kotlin_module",
                "META-INF/*.version",
                "kotlin-tooling-metadata.json",
                "DebugProbesKt.bin",
            )
        }
        jniLibs {
            // Store the .so uncompressed and page-aligned. The APK is a
            // little larger on disk, but the installer can then mmap the
            // library straight out of the APK instead of extracting a
            // second copy into /data/app/.../lib -- which is what
            // actually counts against the user's storage, and what shows
            // up in the app's "size on device".
            useLegacyPackaging = false
        }
    }
}

dependencies {

}
