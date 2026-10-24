#pragma once
// 对话领域测试支持：仅依赖标准库和独立 dialogue 目标，构造有循环出口的最小分支图。
#include <designer/dialogue/dialogue.hpp>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace d = designer::dialogue;
#define CHECK(condition)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        if (!(condition))                                                                                    \
        {                                                                                                    \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #condition "\n";                                \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)
inline d::Definition dialogue_definition(std::uint32_t version = 1)
{
    return {5001,
            version,
            1,
            400,
            {{1, d::NodeKind::text, 7, "npc.hello", 2},
             {2, d::NodeKind::choice, 7, "npc.question", 0, {{20, "answer.yes", 3}, {10, "answer.again", 1}}},
             {3, d::NodeKind::end}}};
}
inline d::Command start(d::RequestId request = 100)
{
    return {request, d::Action::start, 5001};
}
inline d::Command step(d::RequestId request, d::Action action, d::NodeId node, std::uint64_t revision,
                       d::OptionId option = 0, d::SessionId session = 100)
{
    return {request, action, 0, session, node, revision, option};
}
