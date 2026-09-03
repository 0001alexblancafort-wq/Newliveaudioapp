if(NOT TARGET oboe::oboe)
add_library(oboe::oboe SHARED IMPORTED)
set_target_properties(oboe::oboe PROPERTIES
    IMPORTED_LOCATION "/home/codespace/.gradle/caches/transforms-3/e0a383d031039971fdc1f8c69ad37454/transformed/oboe-1.8.1/prefab/modules/oboe/libs/android.arm64-v8a/liboboe.so"
    INTERFACE_INCLUDE_DIRECTORIES "/home/codespace/.gradle/caches/transforms-3/e0a383d031039971fdc1f8c69ad37454/transformed/oboe-1.8.1/prefab/modules/oboe/include"
    INTERFACE_LINK_LIBRARIES ""
)
endif()

