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

include(ExternalProject)

# Download and build mupdf statically
set(MUPDF_VERSION "1.25.0")
set(MUPDF_URL "https://github.com/ArtifexSoftware/mupdf-downloads/releases/download/${MUPDF_VERSION}/mupdf-${MUPDF_VERSION}-source.tar.gz")
set(MUPDF_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/mupdf-ext")

ExternalProject_Add(
    mupdf_external
    URL ${MUPDF_URL}
    PREFIX ${MUPDF_PREFIX}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make HAVE_OBJCOPY=no XCFLAGS=-fPIC HAVE_GLUT=no HAVE_X11=no HAVE_GLFW=no build=release libs
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1
)

set(MUPDF_SRC_DIR "${MUPDF_PREFIX}/src/mupdf_external")
set(MUPDF_BUILD_DIR "${MUPDF_SRC_DIR}/build/release")

# Define imported static library targets
add_library(mupdf STATIC IMPORTED)
set_target_properties(mupdf PROPERTIES IMPORTED_LOCATION "${MUPDF_BUILD_DIR}/libmupdf.a")
add_dependencies(mupdf mupdf_external)

add_library(mupdf-third STATIC IMPORTED)
set_target_properties(mupdf-third PROPERTIES IMPORTED_LOCATION "${MUPDF_BUILD_DIR}/libmupdf-third.a")
add_dependencies(mupdf-third mupdf_external)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MUPDF_SRC_DIR}/include
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
    mupdf
    mupdf-third
    freetype
    harfbuzz
)
