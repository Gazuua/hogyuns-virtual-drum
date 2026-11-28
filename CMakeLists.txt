cmake_minimum_required(VERSION 3.22)

project(HogyunsVirtualDrum VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Release에서는 링크 시점 최적화를 켠다.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# JUCE는 고정 버전(9.0.6)을 external/JUCE 에 내려받아 쓴다.
# 폴더가 없으면 scripts/fetch-juce.ps1 을 먼저 실행한다.
set(JUCE_MODULES_DIR "${CMAKE_SOURCE_DIR}/external/JUCE/modules")

if(NOT EXISTS "${JUCE_MODULES_DIR}/juce_core/juce_core.h")
    message(FATAL_ERROR "external/JUCE not found. Run scripts/fetch-juce.ps1 first.")
endif()

# 코어: 사운드 엔진, 매핑, 키 코드, 이펙트, 반주 재생. JUCE를 쓰지 않아서 따로 자동 테스트한다.
add_library(hvd_core STATIC
    src/core/Chart.cpp
    src/core/DrumTranscriber.cpp
    src/core/Effects.cpp
    src/core/Engine.cpp
    src/core/InputTiming.cpp
    src/core/KeyCode.cpp
    src/core/Mapping.cpp
    src/core/PadKinds.cpp
    src/core/Patterns.cpp
    src/core/Performer.cpp
    src/core/PracticeStats.cpp
    src/core/RhythmGame.cpp
    src/core/TrackPlayer.cpp)

target_include_directories(hvd_core PUBLIC src)
target_compile_options(hvd_core PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8;/W4>")

# JUCE 모듈.
#
# JUCE가 제공하는 CMake 함수(juce_add_gui_app 등)는 쓰지 않고, 필요한 모듈의 소스를 여기서 직접 컴파일한다.
# 그 함수들은 구성 단계에서 보조 도구(juceaide)를 따로 빌드해 실행하는데, 그것 없이도 구성과 빌드가 되게 했다.
# 모듈마다 모듈 폴더 바로 아래의 "<모듈 이름>*.cpp / *.c" 가 컴파일 단위다 (JUCE 모듈 형식).
set(HVD_JUCE_MODULES
    juce_core
    juce_events
    juce_data_structures
    juce_graphics
    juce_gui_basics
    juce_gui_extra
    juce_audio_basics
    juce_audio_devices
    juce_audio_formats
    juce_audio_processors_headless
    juce_audio_processors
    juce_audio_utils)

add_library(hvd_juce OBJECT)

foreach(module IN LISTS HVD_JUCE_MODULES)
    file(GLOB module_sources CONFIGURE_DEPENDS
        "${JUCE_MODULES_DIR}/${module}/${module}*.cpp"
        "${JUCE_MODULES_DIR}/${module}/${module}*.c")

    if(NOT module_sources)
        message(FATAL_ERROR "JUCE module not found: ${module}")
    endif()

    target_sources(hvd_juce PRIVATE ${module_sources})
    target_compile_definitions(hvd_juce PUBLIC JUCE_MODULE_AVAILABLE_${module}=1)
endforeach()

target_include_directories(hvd_juce SYSTEM PUBLIC "${JUCE_MODULES_DIR}")

# 모듈과 앱이 같은 설정으로 컴파일돼야 하므로 PUBLIC으로 둔다.
target_compile_definitions(hvd_juce
    PUBLIC
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_STANDALONE_APPLICATION=1
        $<$<CONFIG:Debug>:DEBUG=1>
        $<$<CONFIG:Debug>:_DEBUG=1>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>
        $<$<NOT:$<CONFIG:Debug>>:_NDEBUG=1>
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # 내장 사운드만 쓰므로 ASIO는 넣지 않는다. WASAPI(공유, 저지연 공유, 독점)를 쓴다.
        JUCE_ASIO=0
        # MP3 샘플과 반주도 읽을 수 있게 한다.
        JUCE_USE_MP3AUDIOFORMAT=1)

target_compile_options(hvd_juce PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/bigobj;/EHsc;/W1>")

# 실행 파일 이름. 프로그램이 켜져 있으면 실행 파일을 덮어쓸 수 없으므로, 그동안에는
# scripts/build.ps1 -ProductName HogyunsVirtualDrumDev 처럼 다른 이름으로 빌드해 검사한다.
set(HVD_PRODUCT_NAME "HogyunsVirtualDrum" CACHE STRING "Name of the executable")

add_executable(HogyunsVirtualDrum WIN32
    src/app/AppSettings.cpp
    src/app/AudioHost.cpp
    src/app/Backups.cpp
    src/app/ChartFile.cpp
    src/app/ChartLibrary.cpp
    src/app/ChartTools.cpp
    src/app/DocumentBar.cpp
    src/app/Documents.cpp
    src/app/DrumSeparator.cpp
    src/app/GameHighway.cpp
    src/app/GamePage.cpp
    src/app/GameRunner.cpp
    src/app/KeymapLoader.cpp
    src/app/KeyboardView.cpp
    src/app/KeysPage.cpp
    src/app/KitLoader.cpp
    src/app/KitPage.cpp
    src/app/KitView.cpp
    src/app/LookAndFeel.cpp
    src/app/Main.cpp
    src/app/MainComponent.cpp
    src/app/MasterGainSlider.cpp
    src/app/MixerPage.cpp
    src/app/MusicFile.cpp
    src/app/PadGrid.cpp
    src/app/PlayLog.cpp
    src/app/PlayPage.cpp
    src/app/PracticeLog.cpp
    src/app/PracticePage.cpp
    src/app/RawKeyboard.cpp
    src/app/SampleCache.cpp
    src/app/Session.cpp
    src/app/SessionLog.cpp
    src/app/SfzImport.cpp
    src/app/TakeRecorder.cpp
    src/app/Theme.cpp
    src/app/TrackPage.cpp
    # 코어 자동 테스트. `--coretests <보고서>` 옵션으로 돌린다.
    tests/CoreTests.cpp)

set_target_properties(HogyunsVirtualDrum PROPERTIES
    OUTPUT_NAME "${HVD_PRODUCT_NAME}"
    # 예전(JUCE CMake 함수로 만들던 때)과 같은 자리에 실행 파일을 둔다.
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/HogyunsVirtualDrum_artefacts/${CMAKE_BUILD_TYPE}")

target_compile_definitions(HogyunsVirtualDrum
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="${HVD_PRODUCT_NAME}"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

# 소스 파일의 한글 문자열을 UTF-8로 읽고 UTF-8로 내보낸다.
target_compile_options(HogyunsVirtualDrum PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8;/W4;/bigobj>")

target_link_libraries(HogyunsVirtualDrum PRIVATE hvd_core hvd_juce)
