#include "application.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv)
{
    try
    {
        std::filesystem::path content = DESIGNER_APP_CONTENT;
        std::filesystem::path save = "saves/player.sav";
        std::filesystem::path script;

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--help")
            {
                std::cout << "designer_game [--content <目录>] [--save <存档路径>] [--script <命令文件>]\n"
                             "已有存档自动恢复；游戏内help查看命令；退出不自动保存。\n";
                return 0;
            }

            if ((arg != "--content" && arg != "--save" && arg != "--script") || i + 1 == argc)
                throw std::invalid_argument("未知参数或缺少参数值，使用--help");
            const std::filesystem::path value = argv[++i];

            if (arg == "--content")
                content = value;
            else if (arg == "--save")
                save = value;
            else
                script = value;
        }

        std::ifstream commands;
        if (!script.empty())
        {
            commands.open(script);
            if (!commands)
                throw std::runtime_error("无法打开命令脚本");
        }

        designer::app::Application application(content, save);

        return application.run(script.empty() ? std::cin : commands, !script.empty());
    }
    catch (const std::exception &e)
    {
        std::cerr << "启动失败：" << e.what() << '\n';
        return 1;
    }
}
