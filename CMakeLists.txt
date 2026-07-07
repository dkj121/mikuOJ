cmake_minimum_required(VERSION 3.16)
project(cppjudge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECCOMP REQUIRED libseccomp)
pkg_check_modules(YAMLCPP REQUIRED yaml-cpp)

add_subdirectory(src)
add_subdirectory(tests)
