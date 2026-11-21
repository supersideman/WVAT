plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.wvat.go"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.wvat.go"
        minSdk = 26          // Android 8.0：与 dsh-Go 对手机的最低要求一致
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

// 刻意不引入任何第三方依赖：蓝牙、媒体键、闹钟、天气全部用系统 API。
// 好处是构建快、失败点少，答辩现场也不怕网络抽风拉不动依赖。
dependencies {
}
