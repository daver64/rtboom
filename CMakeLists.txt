cmake_minimum_required(VERSION 3.24)
project(rtboom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_executable(rtboom
    src/main.cpp
    src/doom/wad_loader.cpp
    src/doom/hud_assets.cpp
    src/doom/sound_assets.cpp
    src/platform/window_gl.cpp
    src/render/shader_program.cpp
    src/render/compute_renderer.cpp
    src/render/present_pass.cpp
)

target_include_directories(rtboom PRIVATE src)

target_link_libraries(rtboom PRIVATE
    SDL3::SDL3
    glm::glm
    glad
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rtboom PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET rtboom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:rtboom>/shaders"
)

add_custom_command(TARGET rtboom POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:rtboom>/assets"
)
