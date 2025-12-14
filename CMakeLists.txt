cmake_minimum_required(VERSION 3.26)

project(cobble)

set(SUPPORTED_PLATFORMS Darwin Linux)
if(NOT CMAKE_SYSTEM_NAME IN_LIST SUPPORTED_PLATFORMS)
  message(WARNING "Platform \"${CMAKE_SYSTEM_NAME}\" is unsupported")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)

if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
  set(CMAKE_OSX_ARCHITECTURES arm64)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 13.5)
endif()

include(./cmake/ExternalPackage.cmake)
include(./cmake/NodeModule.cmake)

if(BUILD_DEPS)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL Darwin)
    ExternalPackage_Add(
      ZLIB
      URL https://zlib.net/fossils/zlib-1.3.1.tar.gz
      HASH 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
      TEST
    )

    ExternalPackage_Get_Directory(ZLIB ZLIB_ROOT)
    set(
      PNG_CMAKE_ARGS
      -D "ZLIB_ROOT=${ZLIB_ROOT}"
      -D ZLIB_USE_STATIC_LIBS=TRUE
    )
  endif()

  ExternalPackage_Add(
    libjpeg-turbo
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.3/libjpeg-turbo-3.1.3.tar.gz
    HASH 075920b826834ac4ddf97661cc73491047855859affd671d52079c6867c1c6c0
    CMAKE_ARGS
      -D ENABLE_SHARED=TRUE
      -D ENABLE_STATIC=TRUE
      -D WITH_SIMD=TRUE
      -D WITH_TESTS=TRUE
      -D WITH_TURBOJPEG=TRUE
    TEST
  )

  ExternalPackage_Add(
    PNG
    URL https://download.sourceforge.net/libpng/libpng-1.6.53.tar.gz
    HASH da0b045cbb1d06a8fc9696f9441359f70645f280ff24ae453ccb7c722353654f
    CMAKE_ARGS
      -D PNG_FRAMEWORK=FALSE
      -D PNG_HARDWARE_OPTIMIZATIONS=TRUE
      -D PNG_SHARED=TRUE
      -D PNG_STATIC=TRUE
      -D PNG_TESTS=TRUE
      -D PNG_TOOLS=FALSE
      ${PNG_CMAKE_ARGS}
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
      -D WEBP_BUILD_FUZZTEST=FALSE
      -D WEBP_BUILD_GIF2WEBP=FALSE
      -D WEBP_BUILD_IMG2WEBP=FALSE
      -D WEBP_BUILD_LIBWEBPMUX=TRUE
      -D WEBP_BUILD_VWEBP=FALSE
      -D WEBP_BUILD_WEBPINFO=FALSE
      -D WEBP_BUILD_WEBPMUX=FALSE
      -D WEBP_BUILD_WEBP_JS=FALSE
      -D WEBP_ENABLE_SIMD=TRUE
      -D WEBP_LINK_STATIC=TRUE
  )

  return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL Darwin)
  ExternalPackage_Get_Directory(ZLIB ZLIB_ROOT)
  set(ZLIB_USE_STATIC_LIBS TRUE)
endif()

ExternalPackage_Find(libjpeg-turbo)
ExternalPackage_Find(PNG)
ExternalPackage_Find(WebP)
NodeModule_Find(node-addon-api)
NodeModule_Find(node-api-headers)

add_library(cobble MODULE)
target_sources(cobble PRIVATE ./src/cobble.cpp)

target_compile_options(cobble PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_SYSTEM_NAME STREQUAL Darwin)
  target_link_options(cobble PRIVATE -undefined dynamic_lookup)
endif()

set_target_properties(
  cobble
  PROPERTIES
    PREFIX ""
    SUFFIX .node
    CXX_VISIBILITY_PRESET hidden
)

target_compile_definitions(
  cobble
  PRIVATE
    NAPI_VERSION=10
    NODE_ADDON_API_CPP_EXCEPTIONS
    NODE_ADDON_API_CPP_EXCEPTIONS_ALL
    NODE_ADDON_API_DISABLE_DEPRECATED
    NODE_ADDON_API_REQUIRE_BASIC_FINALIZERS
    NODE_API_NO_EXTERNAL_BUFFERS_ALLOWED
    NODE_API_SWALLOW_UNTHROWABLE_EXCEPTIONS
)

target_link_libraries(
  cobble
  PRIVATE
    libjpeg-turbo::turbojpeg-static
    PNG::png_static
    WebP::webpdecoder
    WebP::webpdemux
    node-addon-api
    node-api-headers
)
