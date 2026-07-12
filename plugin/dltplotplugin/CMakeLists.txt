# DLT Plot Plugin — real-time Plotly dashboard via embedded HTTP server
# Requires Qt Network (already found by scripts/linux/version.cmake)

add_library(dltplotplugin MODULE
    dltplotplugin.cpp)

target_link_libraries(dltplotplugin
    qdlt
    ${QT_PREFIX}::Widgets
    ${QT_PREFIX}::Network)

add_plugin(dltplotplugin)
