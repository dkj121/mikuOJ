#include <vector>

int main() {
    std::vector<char*> blocks;
    for (;;) {
        char* p = new char[1024 * 1024];  // 每次 1MB
        for (int i = 0; i < 1024 * 1024; i += 4096) p[i] = 1;  // 触碰以占用 RSS
        blocks.push_back(p);
    }
}
