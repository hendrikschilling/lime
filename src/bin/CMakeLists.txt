cmake_minimum_required(VERSION 2.6)
project(limeview)

include_directories(${ELM_INCLUDE_DIRS})
include_directories(${EXEMPI_INCLUDE_DIRS})
include_directories(${EIO_INCLUDE_DIRS})
include_directories(${EET_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/src/lib)

add_executable(limeview limeview.c cli.c tagfiles.c settings.c)

add_executable(limedo limedo.c cli.c)

target_link_libraries(limeview ${ELM_LIBRARIES} ${EIO_LIBRARIES} ${EXEMPI_LIBRARIES} lime)
target_link_libraries(limedo eina ${EINA_LIBRARIES} lime)

install (TARGETS limeview limedo
         RUNTIME DESTINATION bin)
