# =====================================================================
# Build the CorelNestingEngine DLL (v3, NFP engine).
#
# The engine now depends on Clipper2 (polygon booleans + Minkowski sum
# used to compute the No-Fit-Polygon). Two ways to provide it:
#
#  A) ONLINE (default): CMake downloads Clipper2 automatically.
#       cmake -S . -B build -A x64
#       cmake --build build --config Release
#
#  B) OFFLINE (vendored): drop the Clipper2 repo's CPP folder at
#       engine/third_party/Clipper2/CPP   and CMake will use it without
#       any download. (Get it with: git clone https://github.com/AngusJohnson/Clipper2)
#
# Output: build/Release/CorelNestingEngine.dll  (x64, matches CorelDRAW 2018+)
# =====================================================================
cmake_minimum_required(VERSION 3.16)
project(CorelNestingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- obtain Clipper2 -------------------------------------------------
set(CLIPPER2_UTILS    OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS    OFF CACHE BOOL "" FORCE)

set(_VENDORED "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Clipper2/CPP")
if (EXISTS "${_VENDORED}/CMakeLists.txt")
    message(STATUS "Using vendored Clipper2 at ${_VENDORED}")
    add_subdirectory("${_VENDORED}" clipper2_build)
else()
    message(STATUS "Fetching Clipper2 from GitHub")
    include(FetchContent)
    FetchContent_Declare(clipper2
        GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
        GIT_TAG        main
        SOURCE_SUBDIR  CPP)
    FetchContent_MakeAvailable(clipper2)
endif()

# ---- the DLL ---------------------------------------------------------
add_library(CorelNestingEngine SHARED
    CorelNestingEngine.cpp
    CorelNestingEngine.def)

target_link_libraries(CorelNestingEngine PRIVATE Clipper2)

if (MSVC)
    target_compile_options(CorelNestingEngine PRIVATE /O2 /EHsc /W3)
endif()
