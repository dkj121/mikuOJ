#include <stdio.h>
#include <stdlib.h>

int main() {
    for (;;) {
        char* p = (char*)malloc(1024 * 1024);  // 每次 1MB
        for (int i = 0; i < 1024 * 1024; i += 4096) p[i] = 1;  // 触碰以占用 RSS
    }
}
