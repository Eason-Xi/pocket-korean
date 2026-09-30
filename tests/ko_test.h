// tests/ko_test.h —— 韩语学习应用主机测试共用的断言宏。
// 用自己的 CHECK 而不是 assert：即使有人以 -DNDEBUG 编译，检查也不会被悄悄去掉，
// 失败时还会打印文件行号和表达式，定位更快。
#pragma once

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                           \
        }                                                                      \
    } while (0)
