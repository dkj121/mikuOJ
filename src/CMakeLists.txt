add_library(cppjudge_core STATIC
    # 各 .cpp 文件在后续任务中添加
)

target_include_directories(cppjudge_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(cppjudge_core PUBLIC
    ${LIBSECCOMP_LIBRARIES}
    ${YAMLCPP_LIBRARIES}
    pthread
)
