include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.18
)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.8
)

set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL3 glm glad)

# GLAD exposes glad_add_library via its cmake subdirectory.
FetchContent_GetProperties(glad SOURCE_DIR GLAD_SOURCE_DIR BINARY_DIR GLAD_BINARY_DIR)
add_subdirectory(${GLAD_SOURCE_DIR}/cmake ${GLAD_BINARY_DIR})
glad_add_library(glad REPRODUCIBLE API gl:core=4.3)
