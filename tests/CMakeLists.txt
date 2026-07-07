enable_testing()
find_package(GTest REQUIRED)

# ============================================================
# 测试标签约定（供 CI ctest -L 过滤）
#   -L unit         → 单元测试（Google Test）
#   -L integration  → 集成测试（shell 脚本）
#   -L security     → 安全测试（手动运行）
#
# 注册方式：
#   gtest_discover_tests(test_xxx PROPERTIES LABELS unit)
#   add_test(NAME xxx COMMAND ...)
#   set_tests_properties(xxx PROPERTIES LABELS integration)
# ============================================================

# ============================================================
# 测试标签约定（供 CI ctest -L 过滤）
#   -L unit         → 单元测试（Google Test）
#   -L integration  → 集成测试（shell 脚本）
#   -L security     → 安全测试（手动运行）
#
# 注册方式：
#   gtest_discover_tests(test_xxx PROPERTIES LABELS unit)
#   add_test(NAME xxx COMMAND ...)
#   set_tests_properties(xxx PROPERTIES LABELS integration)
# ============================================================
