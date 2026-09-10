cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

project(alsa_rt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(AUDIO_HW_BUILD_EXAMPLES "Build the alsa_rt quick-start examples" OFF)

find_package(ament_cmake REQUIRED)

find_package(ALSA REQUIRED)
if(NOT ALSA_FOUND)
    message(FATAL_ERROR "ALSA library not found! Please run 'apt install libasound2-dev'")
endif()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SWRESAMPLE REQUIRED IMPORTED_TARGET libswresample libavutil)

add_library(${PROJECT_NAME} STATIC
    src/device/alsa_config.cpp
    src/device/alsa_excption.cpp
    src/device/base.cpp
    src/utils/spsc_ring.cpp
    src/capture/capture_stream.cpp
    src/capture/capture_stream_debug.cpp
    src/playback/standard_stream/standard_stream.cpp
    src/playback/standard_stream/standard_stream_debug.cpp
    src/playback/priority_stream/priority_stream.cpp
    src/playback/priority_stream/priority_stream_debug.cpp
    src/resampler/base.cpp
    src/resampler/resampler.cpp
    src/resampler/format/format.cpp
    src/resampler/format/unpack.cpp
)

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
    ${ALSA_LIBRARIES}
    PkgConfig::SWRESAMPLE
    Threads::Threads
)

install(
    DIRECTORY include/
    DESTINATION include
)

set(EXPORT_NAME ${PROJECT_NAME}Targets)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${EXPORT_NAME}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# alsa_rt::alsa_rt
install(
    EXPORT ${EXPORT_NAME}
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION share/${PROJECT_NAME}/cmake
)

ament_export_targets(${EXPORT_NAME})
ament_export_include_directories(include)

if(AUDIO_HW_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()

ament_package()
