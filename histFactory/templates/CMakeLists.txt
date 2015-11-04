cmake_minimum_required (VERSION 2.6)
project (Plotter)

# Configure paths
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/modules")

# Detect if we are inside a CMSSW env
include(CMSSW)

# Ensure C++11 is available
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

# Find ROOT
find_package(ROOT REQUIRED)
include_directories(${ROOT_INCLUDE_DIR})

find_library(ROOT_GENVECTOR_LIB GenVector PATHS ${ROOT_LIB_DIR})

# Find Python

if(IN_CMSSW)
    # Help cmake finding python inside CMSSW
    execute_process(COMMAND scram tool tag python LIBDIR OUTPUT_VARIABLE PYTHON_LIBRARY_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND scram tool tag python LIB OUTPUT_VARIABLE PYTHON_LIBRARY OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(PYTHON_LIBRARY "${PYTHON_LIBRARY_PATH}/lib${PYTHON_LIBRARY}.so")
    execute_process(COMMAND scram tool tag python INCLUDE OUTPUT_VARIABLE PYTHON_INCLUDE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
    execute_process(COMMAND python-config --prefix OUTPUT_VARIABLE
        PYTHON_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    list(APPEND CMAKE_LIBRARY_PATH "${PYTHON_PREFIX}/lib")
    list(APPEND CMAKE_INCLUDE_PATH "${PYTHON_PREFIX}/include")
endif()

find_package(PythonLibs REQUIRED)
include_directories(${PYTHON_INCLUDE_PATH})

include_directories(${PROJECT_SOURCE_DIR})

# Configure external

#include(BuildExternals)

# Multidraw sources

if(IN_CMSSW)

    # Generate dictionnaries
    include_directories($ENV{CMSSW_BASE}/src)

    add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/classes.h COMMAND
        ${PROJECT_SOURCE_DIR}/generateHeader.sh
        ${PROJECT_BINARY_DIR}/classes.h
        COMMENT "Generating classes.h...")

endif()

set(PLOTTER_SOURCES
    Plotter.cc
    classes.h
    )

add_executable(plotter ${PLOTTER_SOURCES})
set_target_properties(plotter PROPERTIES OUTPUT_NAME "plotter.exe")

# Link libraries
target_link_libraries(plotter ${ROOT_LIBRARIES})
target_link_libraries(plotter ${ROOT_GENVECTOR_LIB})
target_link_libraries(plotter ${PYTHON_LIBRARY})

find_library(TREEWRAPPER_LIB cp3_llbbTreeWrapper PATHS
    "$ENV{CMSSW_BASE}/lib/$ENV{SCRAM_ARCH}" NO_DEFAULT_PATH)
target_link_libraries(plotter ${TREEWRAPPER_LIB})
