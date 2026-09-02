cmake_minimum_required(VERSION 3.22.1)

project(spatialaudio LANGUAGES CXX)

option(SPATIAL_USE_SOFA "Enable libmysofa SOFA processing" ON)

add_library(spatialaudio SHARED
    native_spatial_audio.cpp
)

find_library(log-lib log)

target_include_directories(spatialaudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if (SPATIAL_USE_SOFA)
    set(MYSOFA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libmysofa")
    if (EXISTS "${MYSOFA_ROOT}/CMakeLists.txt")
        set(BUILD_TESTS OFF CACHE BOOL "Disable libmysofa tests for Android" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libmysofa statically for Android" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "Build libmysofa static library for Android" FORCE)
        add_subdirectory("${MYSOFA_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/libmysofa")

        target_include_directories(spatialaudio PRIVATE
            "${MYSOFA_ROOT}/src"
            "${MYSOFA_ROOT}/src/hrtf"
            "${CMAKE_CURRENT_BINARY_DIR}/libmysofa/src"
        )
        target_link_libraries(spatialaudio PRIVATE mysofa-static)
        target_compile_definitions(spatialaudio PRIVATE SPATIAL_USE_SOFA=1)
    else()
        message(WARNING "SPATIAL_USE_SOFA is ON but libmysofa was not found in ${MYSOFA_ROOT}. Falling back to the embedded HRTF profile.")
        target_compile_definitions(spatialaudio PRIVATE SPATIAL_USE_SOFA=0)
    endif()
else()
    target_compile_definitions(spatialaudio PRIVATE SPATIAL_USE_SOFA=0)
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft" "${CMAKE_CURRENT_BINARY_DIR}/pffft")
    target_link_libraries(spatialaudio PRIVATE pffft)
    target_include_directories(spatialaudio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft")
endif()

target_link_libraries(spatialaudio
    ${log-lib}
    android
)

set_target_properties(spatialaudio PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
