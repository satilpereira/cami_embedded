# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/app"
    "${CMAKE_CURRENT_LIST_DIR}/board_hal"
    "${CMAKE_CURRENT_LIST_DIR}/config"
    "${CMAKE_CURRENT_LIST_DIR}/myo_ble"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cami_embedded)
