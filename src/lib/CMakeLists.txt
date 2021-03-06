cmake_minimum_required(VERSION 2.6)
project(liblime)

include_directories(${EINA_INCLUDE_DIRS})
include_directories(${LCMS_INCLUDE_DIRS})
include_directories(${TIFF_INCLUDE_DIRS})
include_directories(${JPEG_INCLUDE_DIRS})
include_directories(${EXIF_INCLUDE_DIRS})
include_directories(${SWSCALE_INCLUDE_DIRS})
include_directories(${RAW_INCLUDE_DIRS})
include_directories(${GSL_INCLUDE_DIRS})
include_directories(${OPENCV_INCLUDE_DIRS})
include_directories(${LENSFUN_INCLUDE_DIRS})
include_directories(${EXIV2_INCLUDE_DIRS})

link_directories(${EINA_LIBRARY_DIRS})
link_directories(${LCMS_LIBRARY_DIRS})
link_directories(${TIFF_LIBRARY_DIRS})
link_directories(${JPEG_LIBRARY_DIRS})
link_directories(${EXIF_LIBRARY_DIRS})
link_directories(${SWSCALE_LIBRARY_DIRS})
link_directories(${RAW_LIBRARY_DIRS})
link_directories(${GSL_LIBRARY_DIRS})
link_directories(${OPENCV_LIBRARY_DIRS})
link_directories(${LENSFUN_LIBRARY_DIRS})
link_directories(${EXIV2_LIBRARY_DIRS})

#add_subdirectory(tvdeconv)
#link_directories(tvdeconv)

#add_definitions(-DTVREG_DECONV)
#add_definitions(-DTVREG_NONGAUSSIAN)
#add_definitions(-DNUM_SINGLE)

add_library(lime SHARED global.c common.c render.c tile.c filter.c meta.c filter_convert.c filter_contrast.c filter_comparator.c filter_load.c filter_savetiff.c filter_sharpen.c filters.c filter_denoise.c filter_loadjpeg.c cache.c meta_array.c filter_gauss.c filter_downscale.c configuration.c filter_memsink.c filter_loadtiff.c filter_pretend.c filter_crop.c filter_simplerotate.c filter_interleave.c filter_savejpeg.c filter_fliprot.c filter_rotate.c filter_loadraw.c libraw_helpers.cpp filter_curves.c opencv_helpers.cpp filter_lensfun.c exif_helpers.cpp)


target_link_libraries(lime ${EINA_LIBRARIES} ${TIFF_LIBRARIES} ${JPEG_LIBRARIES} ${LCMS_LIBRARIES} ${EXIF_LIBRARIES} ${SWSCALE_LIBRARIES} m rt ${CMAKE_THREAD_LIBS_INIT} ${RAW_LIBRARIES} ${GSL_LIBRARIES} ${OPENCV_LIBRARIES} ${LENSFUN_LIBRARIES} ${EXIV2_LIBRARIES} ${raw_helper})

install(TARGETS lime
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib/static
)

configure_file(lime.pc.in lime.pc @ONLY)

install(FILES Lime.h meta_array.h tile.h common.h filters_public.h meta.h cache_public.h render.h configuration.h global.h filter_public.h filter.h
	DESTINATION include/lime)	
	
#this should update pc file at install!
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" .)")
install(FILES lime.pc
	DESTINATION lib/pkgconfig)
