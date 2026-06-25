cmake_minimum_required(VERSION 3.15)
project(GritadorUniverse CXX)
set(CMAKE_CXX_STANDARD 17)

# Ajusta RAYLIB_DIR a tu instalación
find_package(raylib QUIET)
if (raylib_FOUND)
    set(RAYLIB_LIBRARIES raylib)
else()
    set(RAYLIB_DIR "" CACHE PATH "Ruta a raylib")
    find_library(RAYLIB_LIB NAMES raylib PATHS ${RAYLIB_DIR}/lib ${RAYLIB_DIR}/src)
    include_directories(${RAYLIB_DIR}/include ${RAYLIB_DIR}/src)
    set(RAYLIB_LIBRARIES ${RAYLIB_LIB})
endif()

include_directories(include)
include_directories(include/third_party/imgui)
include_directories(include/third_party/rlImGui)
add_definitions(-DNO_FONT_AWESOME)

add_executable(gritador_universe
    src/main.cpp
    include/third_party/imgui/imgui.cpp
    include/third_party/imgui/imgui_draw.cpp
    include/third_party/imgui/imgui_tables.cpp
    include/third_party/imgui/imgui_widgets.cpp
    include/third_party/rlImGui/rlImGui.cpp
)
target_link_libraries(gritador_universe ${RAYLIB_LIBRARIES})

if(UNIX AND NOT APPLE)
    target_link_libraries(gritador_universe m dl pthread)
elseif(WIN32)
    target_link_libraries(gritador_universe opengl32 gdi32 winmm)
endif()

# Las texturas se cargan con LoadTexture("archivo.jpg") (ruta relativa al
# working directory), asi que tienen que quedar junto al ejecutable.
file(GLOB GRITADOR_ASSETS "${CMAKE_SOURCE_DIR}/*.jpg" "${CMAKE_SOURCE_DIR}/*.png")
add_custom_command(TARGET gritador_universe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GRITADOR_ASSETS} "$<TARGET_FILE_DIR:gritador_universe>"
)
