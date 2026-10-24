// 独立对话规则：图校验、分支顺序、循环修订号、请求重放、冻结版本、取消和日志恢复。
#include "core_support.hpp"
#include <limits>
int main()
{
    const auto definition = dialogue_definition();
    d::Catalog catalog({definition});
    auto invalid = [](d::Definition bad) {
        bool rejected = false;
        try
        {
            (void)d::validate_dialogue_definition(std::move(bad));
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        CHECK(rejected);
    };
    auto bad = definition;
    bad.entry = 99;
    invalid(bad);
    bad = definition;
    bad.nodes[0].next = 99;
    invalid(bad);
    bad = definition;
    bad.nodes[1].options[0].next = 1;
    invalid(bad); // end 不可达，且循环无出口。
    bad = definition;
    bad.nodes.push_back({4, d::NodeKind::text, 7, "unused", 3});
    invalid(bad);
    bad = definition;
    bad.nodes[1].options[1].id = 20;
    invalid(bad);
    bad = definition;
    bad.nodes[2].next = 1;
    invalid(bad);
    bad = definition;
    bad.nodes[0].text_key = "中文键";
    invalid(bad);
    bad = definition;
    bad.nodes[0].speaker = 0;
    invalid(bad);
    bad = definition;
    bad.nodes[1].options.resize(d::max_options + 1);
    invalid(bad);
    bad = definition;
    bad.nodes[2].id = 2;
    invalid(bad);
    bad = definition;
    bad.nodes[0].kind = static_cast<d::NodeKind>(99);
    invalid(bad);
    auto unsorted = definition;
    std::swap(unsorted.nodes[0], unsorted.nodes[2]);
    CHECK(d::validate_dialogue_definition(unsorted) == definition);
    d::Book book;
    auto reject = [&](const d::Command &command, d::Error error, d::EventId event = 0) {
        const auto before = book.snapshot();
        const auto result = book.execute_dialogue_command(catalog, command, event);
        CHECK(result.error == error && !result.receipt && !result.replayed && book.snapshot() == before);
    };
    reject(start(0), d::Error::invalid_command);
    auto unknown = start();
    unknown.dialogue = 99;
    reject(unknown, d::Error::unknown_dialogue);
    CHECK(book.execute_dialogue_command(catalog, start()).error == d::Error::none);
    reject(start(101), d::Error::active_session);
    reject(step(101, d::Action::choose, 1, 1, 20), d::Error::wrong_node_kind);
    reject(step(101, d::Action::finish, 1, 1), d::Error::wrong_node_kind, 1);
    reject(step(101, d::Action::advance, 1, 1, 0, 999), d::Error::unknown_session);
    CHECK(book.execute_dialogue_command(catalog, step(101, d::Action::advance, 1, 1)).error ==
          d::Error::none);
    CHECK(book.query_dialogue_session(100)->node.options[0].id == 20);
    reject(step(102, d::Action::advance, 1, 1), d::Error::stale_view);
    reject(step(102, d::Action::choose, 2, 2, 99), d::Error::unknown_option);
    CHECK(book.execute_dialogue_command(catalog, step(102, d::Action::choose, 2, 2, 10)).error ==
          d::Error::none);
    reject(step(103, d::Action::advance, 1, 1), d::Error::stale_view); // 回到同节点，也不能使用旧修订。
    CHECK(book.execute_dialogue_command(catalog, step(103, d::Action::advance, 1, 3)).error ==
          d::Error::none);
    CHECK(book.execute_dialogue_command(catalog, step(104, d::Action::choose, 2, 4, 20)).error ==
          d::Error::none);
    reject(step(105, d::Action::advance, 3, 5), d::Error::wrong_node_kind);
    reject(step(105, d::Action::finish, 3, 5), d::Error::invalid_event);
    auto updated = dialogue_definition(2);
    updated.nodes[0].text_key = "npc.new";
    catalog.replace_dialogue_definition(updated);
    CHECK(book.query_dialogue_session(100)->definition.version == 1);
    CHECK(catalog.remove_dialogue_definition(5001));
    CHECK(book.execute_dialogue_command(catalog, step(105, d::Action::finish, 3, 5), 50).error ==
          d::Error::none);
    CHECK(book.execute_dialogue_command(catalog, start()).replayed);
    CHECK(book.execute_dialogue_command(catalog, step(105, d::Action::finish, 3, 5), 999).replayed);
    reject(step(105, d::Action::finish, 3, 6), d::Error::request_conflict);
    reject(step(106, d::Action::cancel, 3, 6), d::Error::session_closed);
    const auto snapshot = book.snapshot();
    d::Book restored;
    CHECK(restored.restore(snapshot) == d::Error::none && restored.snapshot() == snapshot);
    auto corrupt = snapshot;
    corrupt.sessions[0].revision++;
    CHECK(restored.restore(corrupt) == d::Error::invalid_snapshot && restored.snapshot() == snapshot);
    corrupt = snapshot;
    corrupt.commands[2].command.option = 20;
    CHECK(restored.restore(corrupt) == d::Error::invalid_snapshot);
    corrupt = snapshot;
    corrupt.commands[0].sequence = 2;
    CHECK(restored.restore(corrupt) == d::Error::invalid_snapshot);
    corrupt = snapshot;
    corrupt.sessions[0].definition.nodes[0].next = 99;
    CHECK(restored.restore(corrupt) == d::Error::invalid_snapshot);
    catalog.replace_dialogue_definition(updated);
    CHECK(book.execute_dialogue_command(catalog, start(200)).error == d::Error::none);
    CHECK(book.query_dialogue_session(200)->definition.version == 2);
    CHECK(book.execute_dialogue_command(catalog, step(201, d::Action::cancel, 1, 1, 0, 200)).error ==
          d::Error::none);
    CHECK(book.snapshot().commands.back().completion_event == 0);
    bool rejected = false;
    try
    {
        catalog.replace_dialogue_definition(definition);
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    CHECK(rejected && catalog.find(5001)->version == 2);
    catalog.replace_dialogue_definition(updated); // 相同定义重装幂等。
    d::Book small(1);
    CHECK(small.execute_dialogue_command(catalog, start()).error == d::Error::none);
    CHECK(small.execute_dialogue_command(catalog, start()).replayed);
    CHECK(small.execute_dialogue_command(catalog, step(101, d::Action::advance, 1, 1)).error ==
          d::Error::history_full);
    CHECK(small.restore(snapshot) == d::Error::invalid_snapshot);
    d::Book sessions;
    for (std::uint64_t id = 1; id <= d::max_sessions; ++id)
    {
        CHECK(sessions.execute_dialogue_command(catalog, start(id * 2)).error == d::Error::none);
        CHECK(sessions.execute_dialogue_command(catalog, step(id * 2 + 1, d::Action::cancel, 1, 1, 0, id * 2))
                  .error == d::Error::none);
    }
    CHECK(sessions.execute_dialogue_command(catalog, start(1000)).error == d::Error::session_limit);
    std::cout << "Dialogue graph, commands, frozen versions and replay restoration passed\n";
}
