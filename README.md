# CPPJUDGE

单机 C++ 编程竞赛判题系统。运行在 WSL2 上。

## 前置条件
- WSL2，内核 5.15+
- root 权限
- `apt install build-essential cmake libseccomp-dev libyaml-cpp-dev libgtest-dev`

## 构建
```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 快速开始
```
# 环境检查
sudo ./build/cppjudge doctor

# C++ 判题（自动检测语言）
sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/solution.cpp

# Python3 判题
sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/python3/solution.py

# Java 判题
sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/java/Solution.java

# 指定语言（覆盖自动检测）
sudo ./build/cppjudge judge --problem problems/A+B --submission solution.txt --lang python3

# 查看结果
cat build/judge_log.json
```
