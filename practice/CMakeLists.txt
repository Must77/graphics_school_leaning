cmake_minimum_required(VERSION 3.10)
project(PerspectiveDistortionDemo LANGUAGES CXX)

if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)

find_package(glfw3 CONFIG QUIET)
if(NOT glfw3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW3 REQUIRED glfw3)
endif()

find_package(GLEW QUIET)
if(NOT GLEW_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLEW REQUIRED glew)
endif()

add_executable(perspective_distortion
    src/main.cpp
)

target_link_libraries(perspective_distortion
    PRIVATE
        OpenGL::GL
)

if(glfw3_FOUND)
    target_link_libraries(perspective_distortion PRIVATE glfw)
else()
    target_include_directories(perspective_distortion PRIVATE ${GLFW3_INCLUDE_DIRS})
    target_link_directories(perspective_distortion PRIVATE ${GLFW3_LIBRARY_DIRS})
    target_link_libraries(perspective_distortion PRIVATE ${GLFW3_LIBRARIES})
endif()

if(GLEW_FOUND)
    target_link_libraries(perspective_distortion PRIVATE GLEW::GLEW)
else()
    target_include_directories(perspective_distortion PRIVATE ${GLEW_INCLUDE_DIRS})
    target_link_directories(perspective_distortion PRIVATE ${GLEW_LIBRARY_DIRS})
    target_link_libraries(perspective_distortion PRIVATE ${GLEW_LIBRARIES})
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(perspective_distortion PRIVATE dl pthread)
endif()
