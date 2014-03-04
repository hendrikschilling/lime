cmake_minimum_required(VERSION 2.6)
project(limeview)

include_directories(${EINA_INCLUDE_DIRS})
link_directories(${EINA_LIBRARY_DIRS})

include_directories(${ELM_INCLUDE_DIRS})
link_directories(${ELM_LIBRARY_DIRS})

include_directories(${EXEMPI_INCLUDE_DIRS})
link_directories(${EXEMPI_LIBRARY_DIRS})

include_directories(${EIO_INCLUDE_DIRS})
link_directories(${EIO_LIBRARY_DIRS})

include_directories(${EVAS_INCLUDE_DIRS})
link_directories(${EVAS_LIBRARY_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/src/lib)

add_executable(limeview limeview.c cli.c tagfiles.c)

add_executable(limedo limedo.c cli.c)

target_link_libraries(limeview ${ECORE_LIBRARIES} ${EVAS_LIBRARIES} ${ELM_LIBRARIES} ${EIO_LIBRARIES} ${EXEMPI_LIBRARIES} ${EINA_LIBRARIES} lime)
target_link_libraries(limedo  ${EINA_LIBRARIES} lime)

install (TARGETS limeview limedo
         RUNTIME DESTINATION bin)