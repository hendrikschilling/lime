cmake_minimum_required(VERSION 2.6)
project(lime)

INCLUDE(FindPkgConfig)

PKG_CHECK_MODULES (EET REQUIRED eet)
PKG_CHECK_MODULES (ELM REQUIRED elementary)
PKG_CHECK_MODULES (LCMS REQUIRED lcms2)
PKG_CHECK_MODULES (EIO REQUIRED eio)
PKG_CHECK_MODULES (EXIF REQUIRED libexif)
PKG_CHECK_MODULES (EXEMPI REQUIRED exempi-2.0)
PKG_CHECK_MODULES (SWSCALE REQUIRED libswscale)
PKG_CHECK_MODULES (RAW REQUIRED libraw_r)
PKG_CHECK_MODULES (GSL REQUIRED gsl)
PKG_CHECK_MODULES (OPENCV REQUIRED opencv)
PKG_CHECK_MODULES (LENSFUN REQUIRED lensfun)
PKG_CHECK_MODULES (EXIV2 REQUIRED exiv2)
FIND_PACKAGE(TIFF REQUIRED)
FIND_PACKAGE(JPEG REQUIRED)

find_package (Threads)

set(CMAKE_C_FLAGS "-Wall -g -O3 -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS "-Wall -g -O3 -fno-omit-frame-pointer")

include_directories(/usr/include/eina-1/eina)
include_directories(/usr/include/eina-1)
include_directories(/usr/include/efl-1)

add_subdirectory(src/lib)
add_subdirectory(src/bin)
