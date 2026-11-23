cmake_minimum_required(VERSION 3.22)
project(motionstudio_jni LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# android/app/src/main/cpp -> repository root
set(MS_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(MS_CORE_DIR ${MS_REPO_ROOT}/native/cpp)

if(NOT EXISTS ${MS_CORE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "Native engine not found at ${MS_CORE_DIR}")
endif()

set(MS_BUILD_TESTS OFF CACHE BOOL "Skip desktop test executables on Android" FORCE)
add_subdirectory(${MS_CORE_DIR} ${CMAKE_BINARY_DIR}/ms_core EXCLUDE_FROM_ALL)

add_library(motionstudio_jni SHARED ms_jni_bridge.cpp ms_crash.cpp)

target_include_directories(motionstudio_jni PRIVATE
  ${MS_CORE_DIR}
  ${MS_CORE_DIR}/include)

target_link_libraries(motionstudio_jni
  motionstudio_native
  ms_core
  jnigraphics
  log)

# The APK only ever needs what the app calls: place each function in its own
# section and let the linker discard the rest, and keep engine-internal symbols
# out of the exported symbol table.
target_compile_options(motionstudio_jni PRIVATE -ffunction-sections -fdata-sections)
target_link_options(motionstudio_jni PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL)

# The engine core links the shared runtime, so ship it with the APK.
set_target_properties(motionstudio_jni PROPERTIES
  BUILD_RPATH "$ORIGIN"
  INSTALL_RPATH "$ORIGIN")
