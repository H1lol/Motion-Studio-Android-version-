cmake_minimum_required(VERSION 3.16)
project(motionstudio_native LANGUAGES CXX)

# The Android Gradle build only needs the engine libraries, not the desktop
# test executables/CTest registration.
option(MS_BUILD_TESTS "Build the native test executables and register CTest tests" ON)

if(MS_BUILD_TESTS)
  enable_testing()
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MS_CORE_SOURCES
  render/image_buffer.cpp
  render/gpu_mock.cpp
  render/render_pipeline.cpp
  render/render_backends.cpp
  render/render_queue.cpp
  effects/cpu_effects.cpp
  effects/cpu_effects_catalogue.cpp
  effects/cpu_effects_extra.cpp
  effects/cpu_effects_screen.cpp
  effects/cpu_effects_motion.cpp
  effects/cpu_effects_looks.cpp
  effects/effect_stack.cpp
  effects/msp/msp_registry.cpp
  effects/store/effect_store.cpp
  effects/builtin_effects.cpp
  graph/node_graph.cpp
  comp/layer_stack.cpp
  comp/transitions.cpp
  comp/mask.cpp
  comp/transform_ops.cpp
  anim/animation.cpp
  media/media_probe.cpp
  media/ffmpeg_io.cpp
  media/android_decode.cpp
  media/timeline_player.cpp
  media/export_engine.cpp
  media/mlt_timeline.cpp
  media/audio_mixer.cpp
  media/audio_fx.cpp
  media/audio_playback.cpp
  media/loudness.cpp
  color/color_pipeline.cpp
  color/lut.cpp
  color/scopes.cpp
  color/finishing.cpp
  text/text_render.cpp
  graph/node_graph.cpp
  scene/scene.cpp
  dcc/dcc_bridge.cpp
  engine/project_session.cpp
  engine/edit_ops.cpp
  engine/studio_hub.cpp
  tools/copilot_service.cpp
  cache_proxy.cpp
  proxy_registry.cpp
  autosave.cpp
  performance.cpp
  effect_presets.cpp
  effects/openfx_host.cpp
  release_check.cpp
)

# Optional UI sources if present
foreach(f ui/panel_host.cpp ui/panels/edit_panels.cpp ui/panels/all_panels.cpp ui/edit_workspace.cpp ui/error_ui.cpp engine/engine.cpp)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${f})
    list(APPEND MS_CORE_SOURCES ${f})
  endif()
endforeach()

add_library(ms_core STATIC ${MS_CORE_SOURCES})
set_target_properties(ms_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ms_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Android platform media APIs (AMediaCodec / AMediaExtractor) back the decoder
# on device; the desktop build has no equivalent and uses ffmpeg instead.
if(ANDROID)
  find_library(ANDROID_MEDIA_LIB mediandk)
  if(ANDROID_MEDIA_LIB)
    target_link_libraries(ms_core PUBLIC ${ANDROID_MEDIA_LIB} log)
  endif()
endif()
# C ABI used by Flutter FFI.
add_library(motionstudio_native SHARED bridge/ms_bridge.cpp)
target_link_libraries(motionstudio_native PRIVATE ms_core)
target_include_directories(motionstudio_native PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(motionstudio_native PROPERTIES OUTPUT_NAME "motionstudio_native")


add_executable(ms_studio_test tools/ms_studio_test.cpp)
target_link_libraries(ms_studio_test PRIVATE ms_core)

add_executable(ms_pass345_deep tools/ms_pass345_deep_test.cpp)
target_link_libraries(ms_pass345_deep PRIVATE ms_core)

add_executable(ms_pass6_test tools/ms_pass6_test.cpp)
target_link_libraries(ms_pass6_test PRIVATE ms_core)

add_executable(ms_11_16_test tools/ms_11_16_test.cpp)
target_link_libraries(ms_11_16_test PRIVATE ms_core)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/ms_app.cpp)
  add_executable(ms_app tools/ms_app.cpp)
  target_link_libraries(ms_app PRIVATE ms_core)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/ms_shell.cpp)
  add_executable(ms_shell tools/ms_shell.cpp)
  target_link_libraries(ms_shell PRIVATE ms_core)
endif()

add_executable(ms_17_22_test tools/ms_17_22_test.cpp)
target_link_libraries(ms_17_22_test PRIVATE ms_core)

add_executable(ms_23_30_test tools/ms_23_30_test.cpp)
target_link_libraries(ms_23_30_test PRIVATE ms_core)

add_executable(ms_audio_fx_test tools/ms_audio_fx_test.cpp)
target_link_libraries(ms_audio_fx_test PRIVATE ms_core)
add_executable(ms_audio_playback_test tools/ms_audio_playback_test.cpp)
target_link_libraries(ms_audio_playback_test PRIVATE ms_core)
add_executable(ms_comp_graph_test tools/ms_comp_graph_test.cpp)
target_link_libraries(ms_comp_graph_test PRIVATE ms_core)

add_executable(ms_graph_safety_test tools/ms_graph_safety_test.cpp)
target_link_libraries(ms_graph_safety_test PRIVATE ms_core)

add_executable(ms_export_profile_test tools/ms_export_profile_test.cpp)
target_link_libraries(ms_export_profile_test PRIVATE ms_core)

add_executable(ms_edit_test tools/ms_edit_test.cpp)
target_link_libraries(ms_edit_test PRIVATE motionstudio_native)
target_include_directories(ms_edit_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ms_adjustment_test tools/ms_adjustment_test.cpp)
target_link_libraries(ms_adjustment_test PRIVATE motionstudio_native)
target_include_directories(ms_adjustment_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ms_mask_test tools/ms_mask_test.cpp)
target_link_libraries(ms_mask_test PRIVATE motionstudio_native)
target_include_directories(ms_mask_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ms_grade_test tools/ms_grade_test.cpp)
target_link_libraries(ms_grade_test PRIVATE motionstudio_native)
target_include_directories(ms_grade_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ms_clip_look_test tools/ms_clip_look_test.cpp)
target_link_libraries(ms_clip_look_test PRIVATE motionstudio_native)
target_include_directories(ms_clip_look_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ms_title_demo tools/ms_title_demo.cpp)
target_link_libraries(ms_title_demo PRIVATE ms_core)

# Visual harness (not a test): renders real frames to PPM for inspection.
add_executable(ms_shots tools/ms_shots.cpp)
target_link_libraries(ms_shots PRIVATE motionstudio_native)
target_include_directories(ms_shots PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ms_effects_catalogue_test tools/ms_effects_catalogue_test.cpp)
target_link_libraries(ms_effects_catalogue_test PRIVATE ms_core)

add_executable(ms_proxy_test tools/ms_proxy_test.cpp)
target_link_libraries(ms_proxy_test PRIVATE ms_core)

add_executable(ms_engine_load_test tools/ms_engine_load_test.cpp)
target_link_libraries(ms_engine_load_test PRIVATE motionstudio_native)


add_executable(ms_lut_test tools/ms_lut_test.cpp)
target_link_libraries(ms_lut_test PRIVATE ms_core)

add_executable(ms_effects_alpha_test tools/ms_effects_alpha_test.cpp)
target_link_libraries(ms_effects_alpha_test PRIVATE ms_core)

add_executable(ms_effects_screen_test tools/ms_effects_screen_test.cpp)
target_link_libraries(ms_effects_screen_test PRIVATE ms_core)

add_executable(ms_effects_motion_test tools/ms_effects_motion_test.cpp)
target_link_libraries(ms_effects_motion_test PRIVATE ms_core)

add_executable(ms_effects_looks_test tools/ms_effects_looks_test.cpp)
target_link_libraries(ms_effects_looks_test PRIVATE ms_core)

add_executable(ms_loudness_test tools/ms_loudness_test.cpp)
target_link_libraries(ms_loudness_test PRIVATE ms_core)

add_executable(ms_engine_audio_test tools/ms_engine_audio_test.cpp)
target_link_libraries(ms_engine_audio_test PRIVATE motionstudio_native)

add_executable(ms_audio_bridge_test tools/ms_audio_bridge_test.cpp)
target_link_libraries(ms_audio_bridge_test PRIVATE motionstudio_native)
target_include_directories(ms_engine_audio_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Native regression suite (usable with: ctest --test-dir <build> --output-on-failure)
add_test(NAME ms_effects_catalogue_test COMMAND ms_effects_catalogue_test)
add_test(NAME ms_engine_load_test COMMAND ms_engine_load_test)

add_test(NAME ms_studio_test COMMAND ms_studio_test)
add_test(NAME ms_pass345_deep COMMAND ms_pass345_deep)
add_test(NAME ms_pass6_test COMMAND ms_pass6_test)
add_test(NAME ms_11_16_test COMMAND ms_11_16_test)
add_test(NAME ms_17_22_test COMMAND ms_17_22_test)
add_test(NAME ms_23_30_test COMMAND ms_23_30_test)
add_test(NAME ms_audio_fx_test COMMAND ms_audio_fx_test)
add_test(NAME ms_audio_playback_test COMMAND ms_audio_playback_test)
add_test(NAME ms_comp_graph_test COMMAND ms_comp_graph_test)
add_test(NAME ms_graph_safety_test COMMAND ms_graph_safety_test)
add_test(NAME ms_export_profile_test COMMAND ms_export_profile_test)
add_test(NAME ms_engine_audio_test COMMAND ms_engine_audio_test)
add_test(NAME ms_audio_bridge_test COMMAND ms_audio_bridge_test)
add_test(NAME ms_lut_test COMMAND ms_lut_test)
add_test(NAME ms_edit_test COMMAND ms_edit_test)
add_test(NAME ms_proxy_test COMMAND ms_proxy_test)
add_test(NAME ms_adjustment_test COMMAND ms_adjustment_test)
add_test(NAME ms_mask_test COMMAND ms_mask_test)
add_test(NAME ms_grade_test COMMAND ms_grade_test)
add_test(NAME ms_clip_look_test COMMAND ms_clip_look_test)
add_test(NAME ms_effects_alpha_test COMMAND ms_effects_alpha_test)
add_test(NAME ms_effects_screen_test COMMAND ms_effects_screen_test)
add_test(NAME ms_effects_motion_test COMMAND ms_effects_motion_test)
add_test(NAME ms_effects_looks_test COMMAND ms_effects_looks_test)
add_test(NAME ms_loudness_test COMMAND ms_loudness_test)
set_tests_properties(
  ms_studio_test ms_pass345_deep ms_pass6_test ms_11_16_test ms_17_22_test
  ms_23_30_test ms_audio_fx_test ms_audio_playback_test ms_graph_safety_test ms_export_profile_test
  ms_engine_audio_test ms_lut_test ms_edit_test
  PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../.."
)
