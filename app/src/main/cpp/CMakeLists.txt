cmake_minimum_required(VERSION 3.22.1)

project(spatialaudio LANGUAGES CXX)

add_library(spatialaudio SHARED
    native_spatial_audio.cpp
)

find_library(log-lib log)

target_include_directories(spatialaudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(spatialaudio
    ${log-lib}
    android
)

set_target_properties(spatialaudio PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
