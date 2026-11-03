cmake_minimum_required(VERSION 3.14)
project(COMANCHE VERSION 0.1.4)

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../iPlug2" CACHE PATH "iPlug2 root directory")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

iplug_add_plugin(${PROJECT_NAME}
  FORMATS VST3
  SOURCES
    COMANCHE.cpp
    COMANCHE.h
    EffectsChain.cpp
    EffectsChain.h
    SampleLibrary.cpp
    SampleLibrary.h
    SoundClassifier.cpp
    SoundClassifier.h
    PresetManager.cpp
    PresetManager.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Mono-Regular.ttf
    resources/img/horse.png
    resources/img/horse@2x.png
    resources/img/comanche-title.png
    resources/img/comanche-title@2x.png
)

target_link_libraries(${PROJECT_NAME}-vst3 PRIVATE
  "-framework AudioToolbox"
  "-framework CoreFoundation"
)

# iPlug2's CMake builds the NanoVG/Metal renderer as a shared object library without the
# Objective-C class prefix, so its classes (MNVGcontext, MNVGbuffers, MNVGtexture) keep their
# bare names and clash with any other iPlug2 plugin in the same host (e.g. NoisePanner),
# which can crash the host when the editor opens. Give them COMANCHE's prefix too.
foreach(nvg_obj iPlug2_IGraphics_NanoVG_GL_obj iPlug2_IGraphics_NanoVG_Metal_obj)
  if(TARGET ${nvg_obj})
    target_compile_definitions(${nvg_obj} PRIVATE OBJC_PREFIX=v${PROJECT_NAME} VST3_API)
    target_compile_options(${nvg_obj} PRIVATE "SHELL:-include ${IPLUG2_DIR}/IPlug/IPlugOBJCPrefix.pch")
  endif()
endforeach()

# The Info.plist in resources/ uses Xcode's $(VAR) placeholders, which CMake leaves untouched.
# Hosts that locate the binary through CFBundleExecutable (FL Studio) then get an invalid path
# and crash before the plugin is even created, so write the real values into the built bundle.
if(APPLE AND TARGET ${PROJECT_NAME}-vst3)
  set(_plist "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}-vst3>/Info.plist")
  add_custom_command(TARGET ${PROJECT_NAME}-vst3 POST_BUILD
    COMMAND plutil -replace CFBundleExecutable -string "${PROJECT_NAME}" "${_plist}"
    COMMAND plutil -replace CFBundleName -string "${PROJECT_NAME}" "${_plist}"
    COMMAND plutil -replace CFBundleShortVersionString -string "${PROJECT_VERSION}" "${_plist}"
    COMMAND plutil -replace CFBundleVersion -string "${PROJECT_VERSION}" "${_plist}"
    COMMAND plutil -replace CFBundleGetInfoString -string "${PROJECT_VERSION}, AudioDev" "${_plist}"
    COMMAND plutil -replace CFBundleSupportedPlatforms -json "[\"MacOSX\"]" "${_plist}"
    COMMAND plutil -replace LSMinimumSystemVersion -string "10.15" "${_plist}"
    COMMAND plutil -remove CFBundleIconFile "${_plist}" || true
    VERBATIM)
endif()
