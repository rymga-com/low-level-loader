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

val sourceSets = the<SourceSetContainer>()

tasks.test {
    enabled = false
}

tasks.register<JavaExec>("protocolSelfTest") {
    dependsOn(tasks.named("testClasses"))
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("com.rymga.loader.protocol.ProtocolSelfTest")
}

tasks.named("check") {
    dependsOn("protocolSelfTest")
}

tasks.register<JavaExec>("printProtocolVector") {
    dependsOn(tasks.named("testClasses"))
    classpath = sourceSets["test"].runtimeClasspath
    mainClass.set("com.rymga.loader.protocol.ProtocolVectorPrinter")
}
