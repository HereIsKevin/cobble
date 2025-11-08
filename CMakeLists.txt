cmake_minimum_required(VERSION 4.1)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_FLAGS_RELEASE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_OSX_ARCHITECTURES arm64)
set(CMAKE_OSX_DEPLOYMENT_TARGET 13.5)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

include(ExternalPackage)
include(NodeModule)

project(cobble)

ExternalPackage_Start()
    ExternalPackage_Add(
        libjpeg-turbo
        URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.2/libjpeg-turbo-3.1.2.tar.gz
        HASH 8f0012234b464ce50890c490f18194f913a7b1f4e6a03d6644179fa0f867d0cf
        CMAKE_ARGS
            -D ENABLE_SHARED=TRUE
            -D ENABLE_STATIC=TRUE
            -D WITH_TESTS=TRUE
            -D WITH_TURBOJPEG=TRUE
        TEST
    )

    ExternalPackage_Add(
        PNG
        URL https://download.sourceforge.net/libpng/libpng-1.6.50.tar.gz
        HASH 708f4398f996325819936d447f982e0db90b6b8212b7507e7672ea232210949a
        CMAKE_ARGS
            -D PNG_FRAMEWORK=FALSE
            -D PNG_SHARED=TRUE
            -D PNG_STATIC=TRUE
            -D PNG_TESTS=TRUE
            -D PNG_TOOLS=FALSE
        TEST
    )

    ExternalPackage_Add(
        WebP
        URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
        HASH e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
        CMAKE_ARGS
            -D WEBP_BUILD_ANIM_UTILS=FALSE
            -D WEBP_BUILD_CWEBP=FALSE
            -D WEBP_BUILD_DWEBP=FALSE
            -D WEBP_BUILD_EXTRAS=FALSE
            -D WEBP_BUILD_GIF2WEBP=FALSE
            -D WEBP_BUILD_IMG2WEBP=FALSE
            -D WEBP_BUILD_LIBWEBPMUX=TRUE
            -D WEBP_BUILD_VWEBP=FALSE
            -D WEBP_BUILD_WEBPINFO=FALSE
            -D WEBP_BUILD_WEBPMUX=FALSE
            -D WEBP_LINK_STATIC=TRUE
    )
ExternalPackage_End()

NodeModule_Add(node-addon-api)
NodeModule_Add(node-api-headers)

add_library(cobble MODULE)
set_target_properties(cobble PROPERTIES PREFIX "")
set_target_properties(cobble PROPERTIES SUFFIX ".node")
target_sources(cobble PRIVATE ./src/cobble.cpp)
target_compile_options(cobble PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(cobble PRIVATE -undefined dynamic_lookup)
target_link_libraries(
    cobble
    PRIVATE
        libjpeg-turbo::turbojpeg-static
        PNG::png_static
        WebP::webp
        node-addon-api
        node-api-headers
)
