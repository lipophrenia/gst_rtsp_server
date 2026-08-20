cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(rtsp_server LANGUAGES CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GSTREAMER_VIDEO REQUIRED IMPORTED_TARGET gstreamer-video-1.0)
pkg_check_modules(GSTRTSP REQUIRED IMPORTED_TARGET gstreamer-rtsp-server-1.0)
pkg_check_modules(GSTRTSP_CORE REQUIRED IMPORTED_TARGET gstreamer-rtsp-1.0)

add_executable(rtsp_server
    include/config/app_config.hpp
    include/gst/gst_raii.hpp
    include/media/camera_pipeline_builder.hpp
    include/media/element_assembly.hpp
    include/media/media_factory.hpp
    include/media/media_pipeline_builder.hpp
    include/media/mkv_pipeline_builder.hpp
    include/probe/mkv_probe.hpp
    include/server/rtsp_server_app.hpp
    src/main.cpp
    src/config/app_config.cpp
    src/media/camera_pipeline_builder.cpp
    src/media/element_assembly.cpp
    src/media/media_factory.cpp
    src/media/media_pipeline_builder.cpp
    src/media/mkv_pipeline_builder.cpp
    src/probe/mkv_probe.cpp
    src/server/rtsp_server_app.cpp
)

target_compile_features(rtsp_server PRIVATE cxx_std_11)

target_include_directories(rtsp_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rtsp_server PRIVATE
    PkgConfig::GLIB
    PkgConfig::GSTREAMER
    PkgConfig::GSTREAMER_VIDEO
    PkgConfig::GSTRTSP
    PkgConfig::GSTRTSP_CORE
)
