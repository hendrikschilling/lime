cmake_minimum_required(VERSION 2.6)
project(lime)

INCLUDE(TestBigEndian)

TEST_BIG_ENDIAN(BIGENDIAN)
IF(${BIGENDIAN})
     message(FATAL_ERROR "only little-endian architectures supported at the moment, contact the developer")
ENDIF(${BIGENDIAN})


INCLUDE(FindPkgConfig)

PKG_CHECK_MODULES (EINA REQUIRED eina)
PKG_CHECK_MODULES (EVAS REQUIRED evas)
PKG_CHECK_MODULES (ECORE REQUIRED ecore)
PKG_CHECK_MODULES (ELM REQUIRED elementary)
PKG_CHECK_MODULES (LCMS REQUIRED lcms2)
PKG_CHECK_MODULES (EIO REQUIRED eio)
PKG_CHECK_MODULES (EXIF REQUIRED libexif)
PKG_CHECK_MODULES (EXEMPI REQUIRED exempi-2.0)
FIND_PACKAGE(TIFF REQUIRED)
FIND_PACKAGE(JPEG REQUIRED)

find_package (Threads)

set(CMAKE_C_FLAGS "-Wall -g -O0")

#FIND_PACKAGE( Eina REQUIRED )

#add_subdirectory(utils)

#add_executable(main main.cpp)

add_subdirectory(src/lib)
add_subdirectory(src/bin)
