cmake_minimum_required(VERSION 3.1)

project(fluorine)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_FLAGS
    "-std=c++11 -Wall -Wextra -Wno-unused -Wno-unused-parameter")

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED)
find_library(BOOSTPO_LIBRARY NAMES boost_program_options HINTS /usr/lib/x86_64-linux-gnu)
find_library(BOOSTSYS_LIBRARY NAMES boost_system HINTS /usr/lib/x86_64-linux-gnu)
find_library(BOOSTIOS_LIBRARY NAMES boost_iostreams HINTS /usr/lib/x86_64-linux-gnu)
find_library(BOOSTTH_LIBRARY NAMES boost_thread boost_thread-mt HINTS /usr/lib/x86_64-linux-gnu)

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

add_executable(Fluorine
    src/Fluorine.cpp
    )

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    SET(CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE} -static")
endif()

target_link_libraries(Fluorine fluorine snet)

add_subdirectory(src)
add_subdirectory(t)
add_subdirectory(external/fmt)
add_subdirectory(external/snet)
