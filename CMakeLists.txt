cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(rtsp_server)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindPkgConfig)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTRTSP REQUIRED gstreamer-rtsp-server-1.0)
pkg_check_modules(GSTRTSP_CORE REQUIRED gstreamer-rtsp-1.0)

include_directories(
    ${GLIB_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTRTSP_INCLUDE_DIRS}
    ${GSTRTSP_CORE_INCLUDE_DIRS}
    /usr/include/gstreamer-1.0/gst/
)

file(GLOB_RECURSE CPP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
add_executable(${PROJECT_NAME} ${CPP_SOURCES})

target_link_libraries(${PROJECT_NAME}
    ${GLIB_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    gstvideo-1.0
    ${GSTRTSP_LIBRARIES}
    ${GSTRTSP_CORE_LIBRARIES}
)
