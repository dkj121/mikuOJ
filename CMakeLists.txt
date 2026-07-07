cmake_minimum_required(VERSION 3.16)
project(mikuOJ VERSION 0.1.0
        DESCRIPTION "单机、多语言、安全隔离的竞赛判题系统"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 可执行文件统一输出到 build/ 根（README / 集成脚本用 ./build/cppjudge）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 让 find_package 看到 Homebrew 安装的 CMake 配置（macOS）
if(APPLE)
  foreach(p /opt/homebrew /usr/local)
    if(EXISTS "${p}")
      list(APPEND CMAKE_PREFIX_PATH "${p}")
    endif()
  endforeach()
endif()

# Coverage 构建类型（CI: -DCMAKE_BUILD_TYPE=Coverage）
set(CMAKE_CXX_FLAGS_COVERAGE "-g -O0 --coverage -fprofile-arcs -ftest-coverage" CACHE STRING "")
set(CMAKE_C_FLAGS_COVERAGE   "-g -O0 --coverage -fprofile-arcs -ftest-coverage" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS_COVERAGE    "--coverage" CACHE STRING "")
set(CMAKE_SHARED_LINKER_FLAGS_COVERAGE "--coverage" CACHE STRING "")

find_package(Threads REQUIRED)

include(FetchContent)

# ---- yaml-cpp（题目 JSON 解析）----
find_package(yaml-cpp CONFIG QUIET)
if(NOT yaml-cpp_FOUND)
  FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0)
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(yaml-cpp)
endif()
if(TARGET yaml-cpp::yaml-cpp)
  set(YAMLCPP_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
  set(YAMLCPP_TARGET yaml-cpp)
else()
  message(FATAL_ERROR "yaml-cpp target not found")
endif()

# ---- spdlog（诊断日志）----
find_package(spdlog CONFIG QUIET)
if(NOT spdlog_FOUND)
  FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0)
  FetchContent_MakeAvailable(spdlog)
endif()

# ---- cpptrace（异常调用栈追踪）----
find_package(cpptrace CONFIG QUIET)
if(NOT cpptrace_FOUND)
  FetchContent_Declare(cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG v1.0.4)
  FetchContent_MakeAvailable(cpptrace)
endif()

# ---- libseccomp（仅 Linux 安全后端需要）----
if(UNIX AND NOT APPLE)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBSECCOMP REQUIRED libseccomp)
endif()

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
