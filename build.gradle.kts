plugins {
    base
}

group = "com.rymga"
version = "0.1.0"

tasks.named("assemble") {
    dependsOn(":protocol:assemble")
    dependsOn(":gateway:assemble")
    dependsOn(":client-java:assemble")
    dependsOn(":paper-plugin:assemble")
}

tasks.named("check") {
    dependsOn(":protocol:check")
    dependsOn(":gateway:check")
    dependsOn(":client-java:check")
    dependsOn(":paper-plugin:check")
}
