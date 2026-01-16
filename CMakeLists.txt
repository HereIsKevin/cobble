cmake_minimum_required(VERSION 3.26)

project(cobble)

option(DEPS "Build dependencies, must be run once before building addon")
option(TEST_DEPS "Run tests to verify dependencies are built properly" TRUE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS FALSE)

if(NOT DEFINED ZLIB_USE_STATIC_LIBS)
  if(APPLE)
    set(ZLIB_USE_STATIC_LIBS FALSE)
  else()
    set(ZLIB_USE_STATIC_LIBS TRUE)
  endif()
endif()

include(./cmake/ExternalPackage.cmake)
include(./cmake/NodeModule.cmake)

if(DEPS)
  if(TEST_DEPS)
    set(test_args TEST)
  endif()

  if(ZLIB_USE_STATIC_LIBS)
    if(NOT DEFINED ZLIB_ROOT)
      ExternalPackage_Add(
        ZLIB
        URL https://zlib.net/fossils/zlib-1.3.1.tar.gz
        HASH 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        CMAKE_ARGS
          -D "ZLIB_BUILD_EXAMPLES=${TEST_DEPS}"
        ${test_args}
      )

      ExternalPackage_Get_Directory(ZLIB ZLIB_ROOT)
    endif()

    set(
      png_cmake_args
      -D "ZLIB_ROOT=${ZLIB_ROOT}"
      -D ZLIB_USE_STATIC_LIBS=TRUE
    )
  endif()

  ExternalPackage_Add(
    libjpeg-turbo
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.3/libjpeg-turbo-3.1.3.tar.gz
    HASH 075920b826834ac4ddf97661cc73491047855859affd671d52079c6867c1c6c0
    CMAKE_ARGS
      -D "ENABLE_SHARED=${TEST_DEPS}"
      -D ENABLE_STATIC=TRUE
      -D WITH_SIMD=TRUE
      -D "WITH_TOOLS=${TEST_DEPS}"
      -D "WITH_TESTS=${TEST_DEPS}"
      -D WITH_TURBOJPEG=TRUE
    ${test_args}
  )

  ExternalPackage_Add(
    PNG
    URL https://download.sourceforge.net/libpng/libpng-1.6.54.tar.gz
    HASH 472db714567391842e410090df5a37e0f5b2ec67148a3007678b0482d2ba5219
    CMAKE_ARGS
      -D PNG_FRAMEWORK=FALSE
      -D PNG_HARDWARE_OPTIMIZATIONS=TRUE
      -D "PNG_SHARED=${TEST_DEPS}"
      -D PNG_STATIC=TRUE
      -D "PNG_TESTS=${TEST_DEPS}"
      -D PNG_TOOLS=FALSE
      ${png_cmake_args}
    ${test_args}
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

if(ZLIB_USE_STATIC_LIBS AND NOT DEFINED ZLIB_ROOT)
  ExternalPackage_Get_Directory(ZLIB ZLIB_ROOT)
endif()

ExternalPackage_Find(libjpeg-turbo)
ExternalPackage_Find(PNG)
ExternalPackage_Find(WebP)
NodeModule_Find(node-addon-api)
NodeModule_Find(node-api-headers DEF)

add_library(cobble MODULE)
target_sources(cobble PRIVATE ./src/cobble.cpp)

if(MSVC)
  target_compile_options(cobble PRIVATE /W4)
else()
  target_compile_options(cobble PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
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
