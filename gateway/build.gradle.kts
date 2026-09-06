plugins {
    java
    id("io.quarkus") version "3.33.3.1"
}

java {
    toolchain {
        languageVersion = JavaLanguageVersion.of(21)
    }
}

repositories {
    mavenCentral()
}

dependencies {
    implementation(enforcedPlatform("io.quarkus.platform:quarkus-bom:3.33.3.1"))
    implementation(project(":protocol"))
    implementation("io.quarkus:quarkus-rest")

    testImplementation("io.quarkus:quarkus-junit5")
    testImplementation("io.rest-assured:rest-assured")
}

tasks.test {
    useJUnitPlatform()
    environment("LOADER_LICENSE_PROVIDER_MODE", "mock")
    environment("LOADER_MOCK_ENABLED", "true")
    environment("LOADER_MOCK_LICENSE", "dev-license")
    environment("LOADER_SIGNING_KEY_ID", "dev-2026")
    environment("LOADER_SIGNING_PRIVATE_KEY_PKCS8_BASE64", "MC4CAQAwBQYDK2VwBCIEIJ1hsZ3v/VpguoRK9JLsLMREScVpezJpGXA7rAMcrn9g")
    environment("LOADER_SIGNING_PUBLIC_KEY_X509_BASE64", "MCowBQYDK2VwAyEA11qYAYKxCrfVS/7TyWQHOg7hcvPapiMlrwIaaPcHURo=")
    environment("LOADER_ARTIFACT_ROOT", layout.projectDirectory.dir("src/test/resources/artifacts").asFile.absolutePath)
    environment("LOADER_ARTIFACT_PRODUCT_ID", "demo-plugin")
    environment("LOADER_ARTIFACT_CHANNEL", "stable")
    environment("LOADER_ARTIFACT_ID", "demo-release")
    environment("LOADER_ARTIFACT_FILENAME", "demo-plugin.jar")
}
