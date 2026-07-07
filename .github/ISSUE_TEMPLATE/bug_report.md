---
name: Bug 报告
about: 报告一个缺陷（非安全隔离问题）
title: "[bug] "
labels: bug
---

> ⚠️ 沙箱逃逸 / 隔离绕过 / 信息泄漏请勿在此公开，按 SECURITY.md 私下报告。

## 描述

清晰描述问题。

## 复现步骤

1. 后端（`linux-ns` / `builtin`）与命令：

   ```
   sudo ./build/cppjudge judge --problem ... --submission ...
   ```

2. 期望结果：
3. 实际结果（含 stdout 的 JSON 与相关 stderr / judge_log.json）：

## 环境

- OS / 内核：`uname -a`
- cgroup v2：`mount | grep cgroup2`
- 语言/工具链版本：
- cppjudge 版本 / commit：

## 附加信息

最小复现的提交代码、日志等。
