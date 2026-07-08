# =====================================================================
# Build the CorelNestingEngine DLL.
#
#   Configure x64 (matches CorelDRAW 2018+):
#     cmake -S . -B build -A x64
#     cmake --build build --config Release
#   Configure x86 (only for 32-bit CorelDRAW X6/X7/X8):
#     cmake -S . -B build32 -A Win32
#     cmake --build build32 --config Release
#
# The DLL lands in build/Release/CorelNestingEngine.dll
# =====================================================================
cmake_minimum_required(VERSION 3.15)
project(CorelNestingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(CorelNestingEngine SHARED
    CorelNestingEngine.cpp
    CorelNestingEngine.def       # clean export names
)

if (MSVC)
    target_compile_options(CorelNestingEngine PRIVATE /O2 /EHsc /W3)
endif()

# ---------------------------------------------------------------------
# STAGE 2 (real NFP): when you port Deepnest's minkowski.cc you will need
# Boost.Polygon (header-only) and Clipper. Uncomment and point to them:
#
# find_package(Boost REQUIRED)                       # headers only
# target_include_directories(CorelNestingEngine PRIVATE ${Boost_INCLUDE_DIRS})
# target_sources(CorelNestingEngine PRIVATE nfp/clipper.cpp nfp/minkowski.cpp)
# ---------------------------------------------------------------------
