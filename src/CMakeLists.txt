# 平台无关模块（macOS + Linux 都编译）
set(CPPJUDGE_SOURCES
    version.cpp
    comparator.cpp
    problem.cpp
    language.cpp
    logger.cpp
    log.cpp
    compiler.cpp
    doctor.cpp
    sandbox_common.cpp
    sandbox_builtin.cpp
)

# Linux 专有：安全沙箱后端 + 隔离管理器（Phase B）
if(UNIX AND NOT APPLE)
    list(APPEND CPPJUDGE_SOURCES
        sandbox_linux.cpp
        ns_manager.cpp
        cgroup_manager.cpp
        seccomp_manager.cpp
    )
endif()

add_library(cppjudge_core STATIC ${CPPJUDGE_SOURCES})

target_include_directories(cppjudge_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(cppjudge_core PUBLIC
    ${YAMLCPP_TARGET}
    spdlog::spdlog
    cpptrace::cpptrace
    Threads::Threads
)

if(UNIX AND NOT APPLE)
    target_include_directories(cppjudge_core PRIVATE ${LIBSECCOMP_INCLUDE_DIRS})
    target_link_libraries(cppjudge_core PUBLIC ${LIBSECCOMP_LIBRARIES})
    # clone/setresuid/pivot_root 等需要 GNU 扩展（默认 gnu++20 已含，显式保险）
    target_compile_definitions(cppjudge_core PRIVATE _GNU_SOURCE)
endif()

add_executable(cppjudge main.cpp)
target_link_libraries(cppjudge cppjudge_core)
