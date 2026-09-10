# Quick-start examples for alsa_rt.
#
# These executables link the in-tree alias target alsa_rt::alsa_rt, which is
# declared in the top-level CMakeLists.txt. ALSA headers are exposed publicly
# by the library target, so no extra include paths are needed here.

add_executable(capture_example capture_example.cpp)
target_link_libraries(capture_example PRIVATE alsa_rt::alsa_rt)

add_executable(playback_example playback_example.cpp)
target_link_libraries(playback_example PRIVATE alsa_rt::alsa_rt)

add_executable(playback_priority_example playback_priority_example.cpp)
target_link_libraries(playback_priority_example PRIVATE alsa_rt::alsa_rt)

# Install the example binaries so they can be run after `colcon build`.
install(TARGETS
        capture_example
        playback_example
        playback_priority_example
        DESTINATION lib/${PROJECT_NAME})

