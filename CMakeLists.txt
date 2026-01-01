cmake_minimum_required(VERSION 4.1)
project(FrostyEditor)

set(CMAKE_CXX_STANDARD 23)

add_executable(FrostyEditor src/main.cpp)

file(GLOB_RECURSE IMPL_UNITS "src/*.cpp")
file(GLOB_RECURSE INTERFACE_UNITS "src/*.ixx" "src/*.cppm")

#target_compile_options(
#        ${PROJECT_NAME}
#        PRIVATE
#        "/EHa"
#)

target_include_directories(
        ${PROJECT_NAME}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(
        ${PROJECT_NAME}
        PRIVATE
        ${IMPL_UNITS}
)

target_sources(
        ${PROJECT_NAME}
        PRIVATE
        FILE_SET cxx_modules
        TYPE CXX_MODULES FILES
        ${INTERFACE_UNITS}
)