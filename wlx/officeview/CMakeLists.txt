cmake_minimum_required(VERSION 3.16)
project(officeview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Double Commander plugin settings
set(CMAKE_SHARED_LIBRARY_PREFIX "")
if(UNIX)
    set(CMAKE_SHARED_LIBRARY_SUFFIX ".wlx")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MUPDF REQUIRED IMPORTED_TARGET mupdf)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(officeview SHARED
    src/officeview.cpp
    src/focus/FocusManager.cpp
    src/mupdfwidget.cpp
)

target_link_libraries(officeview
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PkgConfig::MUPDF
)
