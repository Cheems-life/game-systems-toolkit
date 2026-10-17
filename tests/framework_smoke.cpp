// 工程冒烟测试：通过公开头文件调用核心库的版本接口，检查库能编译、链接并运行。
// 只检查版本字符串非空，不验证背包、经济或商城的业务规则。

#include <designer/core/version.hpp>
#include <iostream>

int main() {
    // An explicit check remains active in Release builds (unlike assert).
    if (designer::core::version().empty()) {
        std::cerr << "The public library must expose a non-empty version.\n";
        return 1;
    }
    return 0;
}
