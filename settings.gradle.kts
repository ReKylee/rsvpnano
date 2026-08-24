pluginManagement {
	repositories {
		google()
		mavenCentral()
		gradlePluginPortal()
	}
}

dependencyResolutionManagement {
	repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)

	repositories {
		google()
		mavenCentral()
		ivy("https://nodejs.org/dist") {
			name = "Node.js"
			patternLayout {
				artifact("v[revision]/[artifact]-v[revision]-[classifier].[ext]")
			}
			metadataSources {
				artifact()
			}
			content {
				includeModule("org.nodejs", "node")
			}
		}
		ivy("https://github.com/yarnpkg/yarn/releases/download") {
			name = "Yarn"
			patternLayout {
				artifact("v[revision]/[artifact]-v[revision].[ext]")
			}
			metadataSources {
				artifact()
			}
			content {
				includeModule("com.yarnpkg", "yarn")
			}
		}
		ivy("https://github.com/WebAssembly/binaryen/releases/download") {
			name = "Binaryen"
			patternLayout {
				artifact("version_[revision]/[artifact]-version_[revision]-[classifier].[ext]")
			}
			metadataSources {
				artifact()
			}
			content {
				includeModule("com.github.webassembly", "binaryen")
			}
		}
	}
}

rootProject.name = "rsvpnano"

include(":shared")
include(":conversionCore")
include(":androidApp")
include(":webApp")

project(":shared").projectDir = file("companion/shared")
project(":conversionCore").projectDir = file("companion/conversion")
project(":androidApp").projectDir = file("companion/apps/android")
project(":webApp").projectDir = file("companion/apps/web")
