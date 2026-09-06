import org.gradle.api.tasks.JavaExec
import org.gradle.api.tasks.SourceSetContainer

plugins {
    `java-library`
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

tasks.test {
    failOnNoDiscoveredTests = false
}

val sourceSets = the<SourceSetContainer>()

tasks.register<JavaExec>("applicationConfigSelfTest") {
    dependsOn(tasks.named("testClasses"))
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("com.rymga.loader.ApplicationConfigSelfTest")
}

tasks.register<JavaExec>("bundledNativeLibrarySelfTest") {
    dependsOn(tasks.named("testClasses"))
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("com.rymga.loader.BundledNativeLibrarySelfTest")
}

tasks.named("check") {
    dependsOn("applicationConfigSelfTest", "bundledNativeLibrarySelfTest")
}

val applicationNativeLibrary = providers.gradleProperty("applicationNativeLibrary").orNull
val applicationNativePlatform = providers.gradleProperty("applicationNativePlatform").orNull

val applicationBundle = tasks.register<Jar>("applicationBundle") {
    group = "distribution"
    description = "Builds an executable application loader with JNI bundled for one platform"
    dependsOn(tasks.named("classes"))
    archiveBaseName.set("rymga-application-loader")
    archiveClassifier.set(applicationNativePlatform ?: "native")
    from(sourceSets["main"].output)
    manifest.attributes["Main-Class"] = "com.rymga.loader.ApplicationBootstrap"
    if (applicationNativeLibrary != null && applicationNativePlatform != null) {
        from(file(applicationNativeLibrary)) {
            into("META-INF/rymga/native/$applicationNativePlatform")
        }
        from(file(applicationNativeLibrary).parentFile.resolve("third-party-licenses")) {
            into("META-INF/licenses/native")
        }
    }
    from(rootProject.file("LICENSE")) {
        into("META-INF/licenses")
        rename { "rymga-low-level-loader.txt" }
    }
    doFirst {
        val nativeLibrary = requireNotNull(applicationNativeLibrary) {
            "Pass -PapplicationNativeLibrary=/absolute/path/to/the/JNI/library"
        }
        val platform = requireNotNull(applicationNativePlatform) {
            "Pass a supported -PapplicationNativePlatform=<os>-<arch>"
        }
        require(platform in setOf("linux-x86_64", "linux-aarch64", "windows-x86_64",
                "windows-aarch64", "macos-x86_64", "macos-aarch64")) {
            "Pass a supported -PapplicationNativePlatform=<os>-<arch>"
        }
        require(file(nativeLibrary).isFile && file(nativeLibrary).length() in 1..64L * 1024 * 1024) {
            "applicationNativeLibrary must point to a native library of at most 64 MiB"
        }
        val licenses = file(nativeLibrary).parentFile.resolve("third-party-licenses")
        require(licenses.isDirectory && licenses.listFiles()?.isNotEmpty() == true) {
            "Self-contained native third-party licenses were not generated beside applicationNativeLibrary"
        }
        val expected = when {
            platform.startsWith("windows-") -> "rymga_loader_jni.dll"
            platform.startsWith("macos-") -> "librymga_loader_jni.dylib"
            else -> "librymga_loader_jni.so"
        }
        require(file(nativeLibrary).name == expected) { "Expected native library filename $expected" }
    }
}

tasks.register<JavaExec>("applicationBundleSelfTest") {
    group = "verification"
    description = "Extracts and loads JNI from the generated application bundle"
    dependsOn(applicationBundle, tasks.named("testClasses"))
    classpath = sourceSets["test"].runtimeClasspath + files(applicationBundle.flatMap { it.archiveFile })
    mainClass.set("com.rymga.loader.BundledNativeLibrarySelfTest")
    args(layout.buildDirectory.dir("application-bundle-self-test").get().asFile.absolutePath)
}
