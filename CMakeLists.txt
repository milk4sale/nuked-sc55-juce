cmake_minimum_required(VERSION 3.22)

project(NukedSC55JUCE
    VERSION 0.1.0
    DESCRIPTION "JUCE-based standalone host for the Nuked-SC55 emulator"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

#===============================================================================
# JUCE — prefer a local install (mirrors the convention in GrooveGrid)
#===============================================================================
if(WIN32)
    set(JUCE_LOCAL_PATH "C:/JUCE" CACHE PATH "Local JUCE checkout")
elseif(APPLE)
    set(JUCE_LOCAL_PATH "$ENV{HOME}/JUCE" CACHE PATH "Local JUCE checkout")
else()
    set(JUCE_LOCAL_PATH "$ENV{HOME}/JUCE" CACHE PATH "Local JUCE checkout")
endif()

if(EXISTS "${JUCE_LOCAL_PATH}/CMakeLists.txt")
    message(STATUS "JUCE: using local install at ${JUCE_LOCAL_PATH}")
    add_subdirectory(${JUCE_LOCAL_PATH} "${CMAKE_CURRENT_BINARY_DIR}/JUCE")
else()
    message(STATUS "JUCE: no local install at ${JUCE_LOCAL_PATH}, fetching from GitHub")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#===============================================================================
# Nuked-SC55 backend — built from the submodule under external/Nuked-SC55.
# We intentionally do NOT add_subdirectory() the upstream CMakeLists because
# it pulls SDL2 / RtMidi for its standard frontend. We only need the backend.
#===============================================================================
set(NUKED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/Nuked-SC55)

if(NOT EXISTS "${NUKED_DIR}/src/backend/emu.h")
    message(FATAL_ERROR
        "Nuked-SC55 submodule not found at ${NUKED_DIR}.\n"
        "Did you clone with --recurse-submodules?  Run:\n"
        "    git submodule update --init --recursive")
endif()

# config.h.in expects @CMAKE_PROJECT_VERSION@ etc. — those resolve to this
# project's version (0.1.0). Nuked's own version (0.6.1 at pin time) is
# recorded by the submodule SHA, not by this generated header.
set(NUKED_SOURCE "nuked-sc55-juce" CACHE STRING "Origin string baked into the Nuked build")
set(NUKED_ENABLE_ASIO OFF)
configure_file(
    ${NUKED_DIR}/src/backend/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/backend/config.h
    @ONLY)

add_library(nuked_sc55_backend STATIC
    ${NUKED_DIR}/src/backend/config.cpp
    ${NUKED_DIR}/src/backend/diagnostics.cpp
    ${NUKED_DIR}/src/backend/emu.cpp
    ${NUKED_DIR}/src/backend/file_hashing.cpp
    ${NUKED_DIR}/src/backend/file_io.cpp
    ${NUKED_DIR}/src/backend/lcd.cpp
    ${NUKED_DIR}/src/backend/mcu_interrupt.cpp
    ${NUKED_DIR}/src/backend/mcu_opcodes.cpp
    ${NUKED_DIR}/src/backend/mcu_timer.cpp
    ${NUKED_DIR}/src/backend/mcu.cpp
    ${NUKED_DIR}/src/backend/pcm.cpp
    ${NUKED_DIR}/src/backend/rom_io.cpp
    ${NUKED_DIR}/src/backend/rom.cpp
    ${NUKED_DIR}/src/backend/standard_romsets.cpp
    ${NUKED_DIR}/src/backend/submcu.cpp
    ${NUKED_DIR}/src/backend/sha/sha224-256.c)

target_include_directories(nuked_sc55_backend PUBLIC
    ${NUKED_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/backend)

target_compile_features(nuked_sc55_backend PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(nuked_sc55_backend PRIVATE /w)
    target_compile_definitions(nuked_sc55_backend PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(nuked_sc55_backend PRIVATE -w)
endif()

#===============================================================================
# 88emu (SC-8820) — optional, from the gearmulator submodule under
# external/gearmulator (GPL-3.0). Built as its own CMake project with
# everything but 88emu switched off (no JUCE, no plugins), so it can't clash
# with this project's JUCE. Produces one static archive, lib88emu.a, used
# through 88emu's C interface (c_interface.h).
#===============================================================================
set(GEARMULATOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/gearmulator)
set(_88emu_default OFF)
if(EXISTS "${GEARMULATOR_DIR}/source/ronaldo/88emu/88lib/c_interface.h" AND NOT MSVC)
    set(_88emu_default ON)
endif()
option(NUKED_WITH_88EMU "Build the 88emu core for SC-8820 support (needs external/gearmulator)" ${_88emu_default})

if(NUKED_WITH_88EMU)
    if(NOT EXISTS "${GEARMULATOR_DIR}/source/cpu/dsp56300/source/asmjit/CMakeLists.txt")
        message(FATAL_ERROR
            "NUKED_WITH_88EMU is ON but gearmulator's submodules are missing. Run:\n"
            "    git submodule update --init --depth 1 external/gearmulator\n"
            "    git -C external/gearmulator submodule update --init --depth 1 --recursive \\\n"
            "        source/cpu/dsp56300 source/cpu/mc68k source/3rdparty/cpp-terminal \\\n"
            "        source/3rdparty/RmlUi source/3rdparty/freetype source/3rdparty/portmidi-latest \\\n"
            "        source/3rdparty/lunasvg\n"
            "or configure with -DNUKED_WITH_88EMU=OFF to build without SC-8820.")
    endif()

    include(ExternalProject)
    set(_gm gearmulator)
    set(EMU88_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/gearmulator)
    set(EMU88_LIBRARY ${EMU88_BINARY_DIR}/source/ronaldo/88emu/88lib/lib88emu.a)
    if(CMAKE_BUILD_TYPE)
        set(_emu88_build_type ${CMAKE_BUILD_TYPE})
    else()
        set(_emu88_build_type Release)
    endif()

    ExternalProject_Add(emu88_external
        SOURCE_DIR       ${GEARMULATOR_DIR}
        BINARY_DIR       ${EMU88_BINARY_DIR}
        CMAKE_GENERATOR  ${CMAKE_GENERATOR}
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${_emu88_build_type}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_TESTING=OFF
            -D${_gm}_BUILD_JUCEPLUGIN=OFF
            -D${_gm}_BUILD_JUCEPLUGIN_CLAP=OFF
            -D${_gm}_BUILD_INSTALLER=OFF
            -D${_gm}_SYNTH_OSIRUS=OFF
            -D${_gm}_SYNTH_OSTIRUS=OFF
            -D${_gm}_SYNTH_VAVRA=OFF
            -D${_gm}_SYNTH_XENIA=OFF
            -D${_gm}_SYNTH_NODALRED2X=OFF
            -D${_gm}_SYNTH_JE8086=OFF
            -D${_gm}_SYNTH_88EMU=ON
            -D${_gm}_COMPONENT_DSPBRIDGE=OFF
        BUILD_COMMAND    ${CMAKE_COMMAND} --build <BINARY_DIR> --target 88emu_bundle
        INSTALL_COMMAND  ""
        BUILD_BYPRODUCTS ${EMU88_LIBRARY}
        BUILD_ALWAYS     OFF
        USES_TERMINAL_BUILD ON)

    find_package(Threads REQUIRED)
    add_library(emu88 STATIC IMPORTED GLOBAL)
    set_target_properties(emu88 PROPERTIES IMPORTED_LOCATION ${EMU88_LIBRARY})
    target_include_directories(emu88 INTERFACE ${GEARMULATOR_DIR}/source/ronaldo/88emu/88lib)
    target_link_libraries(emu88 INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
    add_dependencies(emu88 emu88_external)
    message(STATUS "88emu: SC-8820 support ON (gearmulator at ${GEARMULATOR_DIR})")
else()
    message(STATUS "88emu: SC-8820 support OFF")
endif()

#===============================================================================
# Emulator cores behind one interface (src/EmulatorCore.h), shared by the GUI
# app and the test runner.
#===============================================================================
add_library(emu_cores STATIC
    src/EmulatorCore.cpp
    src/NukedCore.cpp)
target_include_directories(emu_cores PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(emu_cores PUBLIC nuked_sc55_backend)
target_compile_features(emu_cores PUBLIC cxx_std_23)
if(NUKED_WITH_88EMU)
    target_sources(emu_cores PRIVATE src/Emu88Core.cpp)
    target_link_libraries(emu_cores PUBLIC emu88)
    target_compile_definitions(emu_cores PUBLIC NUKED_HAVE_88EMU=1)
else()
    target_compile_definitions(emu_cores PUBLIC NUKED_HAVE_88EMU=0)
endif()

#===============================================================================
# Standalone JUCE GUI app
#===============================================================================
juce_add_gui_app(NukedSC55JUCE
    PRODUCT_NAME "Nuked SC-55 JUCE"
    COMPANY_NAME "nuked-sc55-juce")

target_sources(NukedSC55JUCE PRIVATE
    src/LevelMeter.cpp
    src/Main.cpp
    src/MainComponent.cpp
    src/MidiFilePlayer.cpp
    src/NukedEngine.cpp
    src/PlayerControls.cpp
    src/PreferencesDialog.cpp
    src/SeekBar.cpp)

target_include_directories(NukedSC55JUCE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(NukedSC55JUCE PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WIN_PER_MONITOR_DPI_AWARE=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:NukedSC55JUCE,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:NukedSC55JUCE,JUCE_VERSION>")

target_link_libraries(NukedSC55JUCE PRIVATE
    emu_cores
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

#===============================================================================
# Headless test runner — renders a MIDI file offline through Nuked-SC55 using
# the same settings the GUI app saves, writes test-output.wav, and emits a
# detailed test-runner.log. No audio device, no UI, fully deterministic.
#===============================================================================
juce_add_console_app(NukedTestRunner
    PRODUCT_NAME "Nuked SC-55 TestRunner")

target_sources(NukedTestRunner PRIVATE
    src/TestRunner.cpp)

target_include_directories(NukedTestRunner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(NukedTestRunner PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(NukedTestRunner PRIVATE
    emu_cores
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_gui_basics  # needed for ScopedJuceInitialiser_GUI / PropertiesFile defaults
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
