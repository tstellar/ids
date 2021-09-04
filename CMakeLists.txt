cmake_minimum_required(VERSION 3.20)
project(libtool
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_INLINES_HIDDEN yes)

find_package(LLVM REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in ${LLVM_DIR}")

find_package(Clang REQUIRED)
message(STATUS "Found Clang ${Clang_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake in ${Clang_DIR}")

add_subdirectory(Sources)
