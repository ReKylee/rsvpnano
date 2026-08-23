import org.jetbrains.kotlin.gradle.ExperimentalWasmDsl

plugins {
	alias(libs.plugins.kotlin.multiplatform)
	alias(libs.plugins.android.library)
}

kotlin {
	androidTarget()

	@OptIn(ExperimentalWasmDsl::class)
	wasmJs {
		browser()
	}

	iosArm64()
	iosSimulatorArm64()

	jvmToolchain(17)

	sourceSets {
		commonMain.dependencies {
			implementation(libs.korlibs.compression)
			implementation(libs.ksoup)
		}

		commonTest.dependencies {
			implementation(kotlin("test"))
		}
	}
}

android {
	namespace = "com.rsvpnano.conversioncore"

	compileSdk = 36

	defaultConfig {
		minSdk = 24
	}

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_17
		targetCompatibility = JavaVersion.VERSION_17
	}
}
