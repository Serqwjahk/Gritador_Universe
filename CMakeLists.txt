cmake_minimum_required(VERSION 3.15)
project(GritadorUniverse C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Raylib
# ============================================================
find_package(raylib QUIET)

if(NOT raylib_FOUND)
    include(FetchContent)

    option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5
    )

    FetchContent_MakeAvailable(raylib)
endif()

# ============================================================
# Includes propios
# ============================================================
include_directories(include)
include_directories(include/third_party/imgui)
include_directories(include/third_party/rlImGui)

add_definitions(-DNO_FONT_AWESOME)

# ============================================================
# Ejecutable
# ============================================================
add_executable(gritador_universe
    src/main.cpp
    include/third_party/imgui/imgui.cpp
    include/third_party/imgui/imgui_draw.cpp
    include/third_party/imgui/imgui_tables.cpp
    include/third_party/imgui/imgui_widgets.cpp
    include/third_party/rlImGui/rlImGui.cpp
)

target_link_libraries(gritador_universe PRIVATE raylib)

# ============================================================
# Librerías del sistema
# ============================================================
if(UNIX AND NOT APPLE)
    target_link_libraries(gritador_universe PRIVATE m dl pthread)
elseif(WIN32)
    target_link_libraries(gritador_universe PRIVATE opengl32 gdi32 winmm)

    if(MINGW)
        target_link_options(gritador_universe PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
endif()

# ============================================================
# Assets junto al ejecutable
# ============================================================
file(GLOB GRITADOR_ASSETS
    "${CMAKE_SOURCE_DIR}/*.jpg"
    "${CMAKE_SOURCE_DIR}/*.jpeg"
    "${CMAKE_SOURCE_DIR}/*.png"
    "${CMAKE_SOURCE_DIR}/*.bmp"
    "${CMAKE_SOURCE_DIR}/*.tga"
)

# Skybox del agujero negro (6 caras separadas) y textura de disco de acrecion
add_custom_command(TARGET gritador_universe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/skybox_nebula_dark"
        "$<TARGET_FILE_DIR:gritador_universe>/skybox_nebula_dark"
    COMMENT "Copiando skybox_nebula_dark junto al ejecutable"
)

if(GRITADOR_ASSETS)
    add_custom_command(TARGET gritador_universe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GRITADOR_ASSETS}
            "$<TARGET_FILE_DIR:gritador_universe>"
        COMMENT "Copiando assets junto al ejecutable"
    )
endif()

# ============================================================
# Export/install local
# ============================================================
install(TARGETS gritador_universe
    RUNTIME DESTINATION .
)

if(GRITADOR_ASSETS)
    install(FILES ${GRITADOR_ASSETS}
        DESTINATION .
    )
endif()