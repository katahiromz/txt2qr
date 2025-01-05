# CMakeLists.txt --- CMake project settings
#    ex) cmake -G "Visual Studio 9 2008" .
#    ex) cmake -DCMAKE_BUILD_TYPE=Release -G "MSYS Makefiles" .
##############################################################################

# CMake minimum version
cmake_minimum_required(VERSION 3.6)

# CMake policy
if(COMMAND cmake_policy)
    cmake_policy(SET CMP0003 NEW)
    cmake_policy(SET CMP0048 NEW)
endif(COMMAND cmake_policy)

# project name and languages
project(txt2qr VERSION 0.7 LANGUAGES C CXX RC)

# set output directory (build/)
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/build)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})

# Visual C++?
if (MSVC)
    add_definitions(-DMSVC)
endif()

##############################################################################

# libpng
find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIR} zbar/include)

# subdirectories
subdirs(zbar libqrencode)

# Unicode
add_definitions(${PNG_DEFINITIONS})

# txt2qr.exe
add_executable(txt2qr WIN32 txt2qr.cpp txt2qr_res.rc)
target_link_libraries(txt2qr PRIVATE comctl32 comdlg32 shlwapi gdi32 zbar gdiplus ${PNG_LIBRARIES})
target_compile_definitions(txt2qr PRIVATE UNICODE _UNICODE)

##############################################################################
