cmake_minimum_required(VERSION 4.1)
project(FrostyEditor)

set(CMAKE_CXX_STANDARD 23)
#set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(FrostyEditor)


file(GLOB_RECURSE IMPL_UNITS "src/*.cpp")
file(GLOB_RECURSE INTERFACE_UNITS "src/*.ixx" "src/*.cppm")

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

add_subdirectory(FrostyCore)

target_link_libraries(
        ${PROJECT_NAME}
        PUBLIC
        FrostyCore
)

target_compile_definitions(
        FrostyCore
        PUBLIC
        "FrostyDefineMain=1"
        "GLM_FORCE_DEPTH_ZERO_TO_ONE"
)

target_compile_definitions(
        ${PROJECT_NAME}
        PUBLIC
        "VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1"
        "GLM_FORCE_DEPTH_ZERO_TO_ONE"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying runtime assets to build directory..."
)