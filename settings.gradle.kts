rootProject.name = "rymga-low-level-loader"

include("protocol")
include("gateway")
include("client-java")
project(":client-java").projectDir = file("client/java")
include("paper-plugin")
project(":paper-plugin").projectDir = file("client/paper")
