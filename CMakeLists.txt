cmake_minimum_required(VERSION 3.14)
project(COMANCHE VERSION 1.0.0)

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
    MacroController.h
    PresetManager.cpp
    PresetManager.h
    resources/resource.h
  RESOURCES
    resources/fonts/Roboto-Mono-Regular.ttf
)
