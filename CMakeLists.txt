cmake_minimum_required(VERSION 3.1)

project(fluorine)
include(ExternalProject)

option(USE_TCMALLOC "Use the tcmalloc library" ON)
option(USE_ASAN "Run with the address sanitizer" OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_FLAGS
    "-std=c++11 -Wall -Wextra -Wno-unused -Wno-unused-parameter")

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -Wl,--whole-archive -lpthread -Wl,--no-whole-archive")
endif()

# Run with: ASAN_OPTIONS=symbolize=1 ASAN_SYMBOLIZER_PATH=$(shell which llvm-symbolizer)
# set(ASAN_CXX_FLAGS "-O -g -lasan -fsanitize=address -fno-omit-frame-pointer")
set(ASAN_CXX_FLAGS "")
if (USE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_CXX_FLAGS}")
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${ASAN_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")

set(GPERFTOOLS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/gperftools)
set(JEMALLOC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/jemalloc)

ExternalProject_Add(gperftools
    SOURCE_DIR ${GPERFTOOLS_PATH}
    BINARY_DIR ${GPERFTOOLS_PATH}
    STAMP_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/build
    TMP_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/build
    CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${LIBRARY_OUTPUT_PATH}/gperftools
    BUILD_COMMAND make -j 8
    INSTALL_COMMAND make install
    )

ExternalProject_Add(jemalloc
    SOURCE_DIR ${JEMALLOC_PATH}
    BINARY_DIR ${JEMALLOC_PATH}
    STAMP_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/build
    TMP_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/build
    CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=${LIBRARY_OUTPUT_PATH}/jemalloc
    BUILD_COMMAND make -j 8
    INSTALL_COMMAND sh -c "make install || exit 0"
    )

add_library(libtcmalloc UNKNOWN IMPORTED)
add_library(libjemalloc UNKNOWN IMPORTED)

set_target_properties(libtcmalloc PROPERTIES
    IMPORTED_LOCATION ${LIBRARY_OUTPUT_PATH}/gperftools/lib/libtcmalloc_minimal.a
    )
set_target_properties(libjemalloc PROPERTIES
    IMPORTED_LOCATION ${LIBRARY_OUTPUT_PATH}/jemalloc/lib/libjemalloc.a
    )

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED)
find_library(BOOSTPO_LIBRARY NAMES boost_program_options HINTS /usr/lib/x86_64-linux-gnu)
find_library(BOOSTSYS_LIBRARY NAMES boost_system HINTS /usr/lib/x86_64-linux-gnu)
find_library(BOOSTIOS_LIBRARY NAMES boost_iostreams HINTS /usr/lib/x86_64-linux-gnu)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/fmt)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/rapidjson/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/include)

set(HIREDIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/external/hiredis/libhiredis.a)
add_custom_target(build_hiredis ALL
    COMMAND ${CMAKE_MAKE_PROGRAM}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/hiredis
    COMMENT "Built hiredis")
add_library(hiredis STATIC IMPORTED)
set_property(TARGET hiredis APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(hiredis PROPERTIES
    IMPORTED_LOCATION_NOCONFIG "${HIREDIS_LIBRARY}")
add_dependencies(hiredis build_hiredis)

set(GZSTREAM_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/external/gzstream/libgzstream.a)
add_custom_target(build_gzstream ALL
    COMMAND ${CMAKE_MAKE_PROGRAM}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/gzstream
    COMMENT "Built gzstream")
add_library(gzstream STATIC IMPORTED)
set_property(TARGET gzstream APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gzstream PROPERTIES
    IMPORTED_LOCATION_NOCONFIG "${GZSTREAM_LIBRARY}")
add_dependencies(gzstream build_gzstream)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux" AND NOT USE_ASAN)
    set(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE} -static")
endif()

if (USE_TCMALLOC)
    set(MALLOC_LIBRARY libtcmalloc)
else()
    set(MALLOC_LIBRARY libjemalloc)
endif()


message(STATUS "Malloc library: ${MALLOC_LIBRARY}")
message(STATUS "Use ASAN: ${USE_ASAN}")


add_executable(Fluorine
    src/Fluorine.cpp
    )

target_link_libraries(Fluorine fluorine snet ${MALLOC_LIBRARY})

add_subdirectory(src)
add_subdirectory(t)
add_subdirectory(util)
add_subdirectory(external/fmt)
add_subdirectory(external/snet)
