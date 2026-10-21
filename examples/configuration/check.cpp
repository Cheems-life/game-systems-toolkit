// 配置构建检查：只加载和校验，不创建玩家；失败输出可定位诊断并返回非零退出码。
#include <designer/configuration/bundle.hpp>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: designer_config_check <game.json>\n"; return 2; }
    const auto loaded = designer::configuration::load(argv[1], designer::quests::default_objectives());
    if (loaded.error) {
        const auto& e = *loaded.error;
        std::cerr << e.source << ": " << e.path;
        if (e.record_id) std::cerr << " (record " << *e.record_id << ')';
        if (e.byte) std::cerr << " byte " << e.byte;
        std::cerr << " [" << designer::configuration::error_name(e.code) << "] " << e.message << '\n';
        return 1;
    }
    std::cout << "Configuration valid: content_version=" << loaded.bundle->content_version()
              << ", products=" << loaded.bundle->products().entries().size()
              << ", quests=" << loaded.bundle->quests().entries().size() << '\n';
}
