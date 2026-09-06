import org.gradle.api.tasks.JavaExec
import org.gradle.api.tasks.SourceSetContainer

plugins {
    java
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

repositories {
    mavenCentral()
    maven("https://repo.papermc.io/repository/maven-public/")
}

dependencies {
    compileOnly("io.papermc.paper:paper-api:1.21.8-R0.1-20250906.215025-55")
    testCompileOnly("io.papermc.paper:paper-api:1.21.8-R0.1-20250906.215025-55")
    implementation(project(":client-java"))
}

val sourceSets = the<SourceSetContainer>()
val clientSourceSets = project(":client-java").extensions.getByType<SourceSetContainer>()

tasks.jar {
    dependsOn(project(":client-java").tasks.named("classes"))
    from(project(":client-java").extensions.getByType<SourceSetContainer>()["main"].output)
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
}

tasks.test {
    enabled = false
}

tasks.register<JavaExec>("paperCredentialsSelfTest") {
    dependsOn(tasks.named("testClasses"))
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("com.rymga.loader.paper.PaperCredentialsSelfTest")
}

tasks.register<Jar>("paperFixtureJar") {
    dependsOn(tasks.named("testClasses"))
    archiveFileName.set("paper-protected-fixture.jar")
    from(sourceSets["test"].output) {
        include("com/rymga/loader/paper/ProtectedPaperFixture.class")
    }
    from("src/test/resources/paper-fixture")
}

tasks.named("check") {
    dependsOn("paperCredentialsSelfTest")
}

val paperNativeLibrary = providers.gradleProperty("paperNativeLibrary").orNull
val paperNativePlatform = providers.gradleProperty("paperNativePlatform").orNull

val paperBundle = tasks.register<Jar>("paperBundle") {
    group = "distribution"
    description = "Builds a Paper plugin with its JNI library bundled for one platform"
    dependsOn(tasks.named("classes"), project(":client-java").tasks.named("classes"))
    archiveBaseName.set("rymga-paper-loader")
    archiveClassifier.set(paperNativePlatform ?: "native")
    from(sourceSets["main"].output)
    from(project(":client-java").extensions.getByType<SourceSetContainer>()["main"].output)
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    if (paperNativeLibrary != null && paperNativePlatform != null) {
        from(file(paperNativeLibrary)) {
            into("META-INF/rymga/native/$paperNativePlatform")
        }
        from(file(paperNativeLibrary).parentFile.resolve("third-party-licenses")) {
            into("META-INF/licenses/native")
        }
    }
    from(rootProject.file("LICENSE")) {
        into("META-INF/licenses")
        rename { "rymga-low-level-loader.txt" }
    }
    doFirst {
        val nativeLibrary = requireNotNull(paperNativeLibrary) {
            "Pass -PpaperNativeLibrary=/absolute/path/to/the/JNI/library"
        }
        val platform = requireNotNull(paperNativePlatform) {
            "Pass a supported -PpaperNativePlatform=<os>-<arch>"
        }
        require(platform in setOf("linux-x86_64", "linux-aarch64", "windows-x86_64",
                "windows-aarch64", "macos-x86_64", "macos-aarch64")) {
            "Pass a supported -PpaperNativePlatform=<os>-<arch>"
        }
        require(file(nativeLibrary).isFile) { "paperNativeLibrary must point to an existing file" }
        require(file(nativeLibrary).length() in 1..64L * 1024 * 1024) {
            "paperNativeLibrary must contain between 1 byte and 64 MiB"
        }
        val licenses = file(nativeLibrary).parentFile.resolve("third-party-licenses")
        require(licenses.isDirectory && licenses.listFiles()?.isNotEmpty() == true) {
            "Self-contained native third-party licenses were not generated beside paperNativeLibrary"
        }
        val expected = when {
            platform.startsWith("windows-") -> "rymga_loader_jni.dll"
            platform.startsWith("macos-") -> "librymga_loader_jni.dylib"
            else -> "librymga_loader_jni.so"
        }
        require(file(nativeLibrary).name == expected) { "Expected native library filename $expected" }
    }
}

tasks.register<JavaExec>("paperBundleSelfTest") {
    group = "verification"
    description = "Extracts and verifies the JNI resource from the generated Paper bundle"
    dependsOn(paperBundle, tasks.named("testClasses"), project(":client-java").tasks.named("testClasses"))
    classpath = sourceSets["test"].runtimeClasspath + clientSourceSets["test"].runtimeClasspath +
            files(paperBundle.flatMap { it.archiveFile })
    mainClass.set("com.rymga.loader.BundledNativeLibrarySelfTest")
    args(layout.buildDirectory.dir("paper-bundle-self-test").get().asFile.absolutePath)
}
