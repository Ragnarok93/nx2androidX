pluginManagement {
    repositories {
        google()
        // CI sometimes receives a missing JAR from repo.maven.apache.org despite
        // the artifact existing (e.g. protobuf-java-util 3.25.5). Prefer the
        // Google Cloud-hosted Maven Central mirror, keeping Central as fallback.
        maven {
            url = uri("https://maven-central.storage-download.googleapis.com/maven2/")
        }
        mavenCentral()
        gradlePluginPortal()
    }
}
plugins {
    id("com.android.application") version "9.3.1" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.20" apply false
    id("com.google.devtools.ksp") version "2.3.6" apply false
    id("com.google.dagger.hilt.android") version "2.60.1" apply false
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        // CI sometimes receives a missing JAR from repo.maven.apache.org despite
        // the artifact existing (e.g. protobuf-java-util 3.25.5). Prefer the
        // Google Cloud-hosted Maven Central mirror, keeping Central as fallback.
        maven {
            url = uri("https://maven-central.storage-download.googleapis.com/maven2/")
        }
        mavenCentral()
    }
}
rootProject.name = "nx2android-phone"
include(":app")
