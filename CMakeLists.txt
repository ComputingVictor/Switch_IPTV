cmake_minimum_required(VERSION 3.13)

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(SwitchIPTV CXX C)

set(PROJECT_AUTHOR "SwitchIPTV contributors")
set(PROJECT_VERSION_STRING "0.2.0")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icon-iptvswitch.jpg)
set(BOREALIS_RESOURCES ${BOREALIS_DIR}/resources)
set(APP_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

add_subdirectory(${BOREALIS_LIBRARY} borealis_build)

file(GLOB APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
list(REMOVE_ITEM APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/main.cpp)
list(APPEND APP_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)

program_target(${PROJECT_NAME} "${APP_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DEVKITPRO}/portlibs/switch/include/freetype2)
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
target_link_directories(${PROJECT_NAME} PRIVATE ${DEVKITPRO}/portlibs/switch/lib)
target_link_libraries(${PROJECT_NAME} PRIVATE borealis SDL2 avformat avcodec dav1d
    swscale swresample avutil curl mbedtls mbedx509 mbedcrypto z bz2 pthread m nx)

add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
    COMMAND ${NX_NACPTOOL_EXE} --create "SwitchIPTV" "${PROJECT_AUTHOR}" "${PROJECT_VERSION_STRING}" ${PROJECT_NAME}.nacp
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/romfs
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES} ${CMAKE_BINARY_DIR}/romfs
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${APP_RESOURCES} ${CMAKE_BINARY_DIR}/romfs
    COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/romfs
)
