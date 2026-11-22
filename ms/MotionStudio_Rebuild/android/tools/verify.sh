#!/usr/bin/env bash
# Build the Android app the way CI would: compile Kotlin, run JVM unit tests,
# run lint, build the debug APK with the native engine for every ABI.
#
# Requirements: JDK 17 and an Android SDK with platform 35, build-tools 35.0.0,
# NDK 27.0.12077973 and CMake 3.22.1. Point JAVA_HOME and ANDROID_HOME at them,
# or let sdkmanager install into ANDROID_HOME.
#
#   JAVA_HOME=/path/to/jdk ANDROID_HOME=/path/to/sdk tools/verify.sh
#
set -euo pipefail

cd "$(dirname "$0")/.."

: "${JAVA_HOME:?set JAVA_HOME to a JDK 17}"
: "${ANDROID_HOME:?set ANDROID_HOME to an Android SDK}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"

# 3 GB is the project default; override for a smaller machine with
#   GRADLE_MEMORY=-Xmx1700m tools/verify.sh
GRADLE_MEMORY="${GRADLE_MEMORY:--Xmx3g -XX:MaxMetaspaceSize=768m}"

exec ./gradlew "$@" --console=plain \
    "-Dorg.gradle.jvmargs=$GRADLE_MEMORY -Dfile.encoding=UTF-8" \
    :app:testDebugUnitTest \
    :app:lintDebug \
    :app:assembleDebug
