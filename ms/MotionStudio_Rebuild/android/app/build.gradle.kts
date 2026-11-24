plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.motionstudio.app"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.motionstudio.app"
        minSdk = 29
        targetSdk = 35
        versionCode = 2
        versionName = "2026.1.0"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-fexceptions", "-frtti")
                arguments += listOf("-DMS_BUILD_TESTS=OFF", "-DANDROID_STL=c++_shared")
            }
        }
        ndk {
            // Arm64 plus the 32-bit ARM slice, so a budget phone that only runs
            // 32-bit binaries can install the same APK. The extra slice costs
            // about 0.6 MB of native code. Build the emulator variant with
            // -Pemulator to swap the 32-bit slice for x86_64.
            abiFilters += if (project.hasProperty("emulator")) {
                listOf("arm64-v8a", "x86_64")
            } else {
                listOf("arm64-v8a", "armeabi-v7a")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        compose = true
        // Diagnostics reports the installed version, so BuildConfig must exist.
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    // AGP compiles Java at 1.8 here, so kotlinc has to match it.
    kotlinOptions {
        jvmTarget = "1.8"
    }

    buildTypes {
        release {
            // The installable build. R8 removes the Compose and material-icons
            // classes the app never references (the extended icon set alone is
            // tens of MB of dex), and shrinkResources drops unused fonts and
            // drawables. Signed with the debug key so it can be sideloaded.
            isMinifyEnabled = true
            isShrinkResources = true
            signingConfig = signingConfigs.getByName("debug")
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    packaging {
        jniLibs {
            // Uncompressed .so files are mapped straight from the APK, so the
            // installer copies nothing and the loader mmaps one file.
            useLegacyPackaging = false
        }
        resources {
            // Nothing reads the merged dependency metadata at runtime.
            excludes += listOf("META-INF/*.version", "META-INF/com/android/build/gradle/**")
        }
    }

    dependenciesInfo {
        // Both blobs only describe the build to Play; they cost space in a
        // sideloaded APK.
        includeInApk = false
        includeInBundle = false
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.12.01")
    implementation(composeBom)
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")

    debugImplementation("androidx.compose.ui:ui-tooling")

    testImplementation("junit:junit:4.13.2")
    // Real org.json on the JVM test classpath; android.jar only has stubs.
    testImplementation("org.json:json:20240303")
}
