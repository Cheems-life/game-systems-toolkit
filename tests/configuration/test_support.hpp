#pragma once
// 配置测试支持：读取固定样例，并在不引入 JSON 库的情况下精确替换测试输入。
#include <designer/configuration/bundle.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)
inline std::string fixture() {
    std::ifstream input(DESIGNER_CONFIG_FIXTURE);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
inline std::string replace(std::string text, const std::string& from, const std::string& to) {
    const auto pos = text.find(from); CHECK(pos != std::string::npos);
    text.replace(pos, from.size(), to); return text;
}
