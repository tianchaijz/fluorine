cmake_minimum_required(VERSION 3.1)

project(fluorine)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_FLAGS
    "-std=c++11 -Wall -Wextra -Wno-unused -Wno-unused-parameter -O2")

set(CMAKE_CXX_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_BINARY_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${PROJECT_BINARY_DIR}/lib")

find_package(Boost REQUIRED)
find_library(BOOSTPO_LIBRARY NAMES boost_program_options)
find_library(BOOSTTH_LIBRARY NAMES boost_thread-mt)
find_library(BOOSTSYS_LIBRARY NAMES boost_system)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/rapidjson/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/include)

add_executable(F
    src/F.cpp
    )
target_link_libraries(F fluorine snet ${BOOSTPO_LIBRARY})

add_subdirectory(src)
add_subdirectory(external/snet)
