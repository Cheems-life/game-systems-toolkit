// 玩家对话事务：完成事实与会话一起提交；任务日志失败、恢复、重放和原有资产操作保持一致。
#include "test_support.hpp"
#include <limits>
int main()
{
    auto p = dialogue_player();
    CHECK(p->accept_quest(1, {5, 10}) == quest::Error::none);
    reach_end(*p);
    CHECK(p->snapshot().quests.events.empty() &&
          p->query_quest_status(1, {5, 10}).status != quest::Status::ready);
    CHECK(p->execute_interaction({100, 7}).error ==
          designer::interaction::Error::none); // 领域请求 ID 分别去重。
    const auto finish = step(103, d::Action::finish, 3, 3);
    auto result = p->execute_dialogue_command(finish);
    CHECK(result.error == d::Error::none && result.receipt->completion_event == 12);
    CHECK(p->query_quest_status(1, {5, 12}).status == quest::Status::ready);
    const auto done = p->snapshot();
    CHECK(done.quests.events.back().fact->subject == 5001);
    CHECK(p->execute_dialogue_command(finish).replayed && equal(done, p->snapshot()));
    CHECK(p->execute_dialogue_command(step(103, d::Action::finish, 3, 4)).error ==
          d::Error::request_conflict);
    CHECK(p->purchase_product(products(), {100, 100, 1, 1}).error == commerce::Error::none);
    CHECK(p->dialogues().query_dialogue_session(100)->status ==
          d::Status::completed); // purchase 的候选也保留对话。
    CHECK(p->claim_quest_reward({101, 1, 1}).error == quest::Error::none);
    auto restored = dialogue_player();
    restored->replace_dialogues(d::Catalog({}));
    restored->replace_quests(quest::QuestCatalog({}));
    const auto original = p->snapshot();
    CHECK(restored->restore(original) == quest::Error::none && equal(original, restored->snapshot()));
    CHECK(restored->execute_dialogue_command(finish).replayed &&
          restored->claim_quest_reward({101, 1, 1}).replayed);
    auto reject = [&](player_api::PlayerSnapshot bad) {
        CHECK(restored->restore(bad) == quest::Error::invalid_snapshot &&
              equal(original, restored->snapshot()));
        std::string keep = "keep";
        CHECK(player_api::encode_archive(bad, keep) == player_api::ArchiveError::malformed && keep == "keep");
    };
    auto bad = original;
    bad.quests.events.back().fact->subject = 7;
    reject(bad);
    bad = original;
    bad.quests.events.pop_back();
    reject(bad);
    bad = original;
    bad.dialogues.commands.back().completion_event = 11;
    reject(bad);
    bad = original;
    bad.dialogues.sessions[0].status = d::Status::active;
    reject(bad);
    // 构造语义各自合法、但交互和对话共用一条相同事实的快照，必须拒绝双重归属。
    auto duplicate = dialogue_player();
    reach_end(*duplicate);
    CHECK(duplicate->execute_dialogue_command(finish).error == d::Error::none);
    bad = duplicate->snapshot();
    bad.interactions.push_back({{999, 5001}, 1, designer::interaction::Mode::repeatable, 400, 1});
    CHECK(duplicate->restore(bad) == quest::Error::invalid_snapshot);
    auto small = dialogue_player(1);
    CHECK(small->accept_quest(1, {5, 0}) == quest::Error::none);
    reach_end(*small);
    CHECK(small->observe_gameplay_fact({1, 999, 99, 1}).error == quest::Error::none);
    const auto before = small->snapshot();
    result = small->execute_dialogue_command(finish);
    CHECK(result.error == d::Error::fact_rejected && result.quest_error == quest::Error::event_history_full &&
          equal(before, small->snapshot()));
    auto retry = dialogue_player();
    retry->replace_dialogues(d::Catalog({}));
    CHECK(retry->restore(before) == quest::Error::none);
    CHECK(retry->execute_dialogue_command(finish).error == d::Error::none &&
          retry->query_quest_status(1, {5, 2}).status == quest::Status::ready);
    auto overflow = dialogue_player();
    reach_end(*overflow);
    CHECK(overflow->observe_gameplay_fact({std::numeric_limits<quest::EventId>::max(), 999, 99, 1}).error ==
          quest::Error::none);
    const auto maxed = overflow->snapshot();
    CHECK(overflow->execute_dialogue_command(finish).error == d::Error::event_id_exhausted &&
          equal(maxed, overflow->snapshot()));
    CHECK(overflow->execute_dialogue_command(step(104, d::Action::cancel, 3, 3)).error ==
          d::Error::none); // 取消无需事件序号。
    auto active = dialogue_player();
    reach_end(*active);
    auto v2 = dialogue_definition(2);
    v2.nodes[0].text_key = "new";
    active->replace_dialogues(d::Catalog({v2}));
    auto saved = active->snapshot();
    auto empty = dialogue_player();
    empty->replace_dialogues(d::Catalog({}));
    CHECK(empty->restore(saved) == quest::Error::none);
    CHECK(empty->dialogues().query_dialogue_session(100)->definition.version == 1 &&
          empty->execute_dialogue_command(finish).error == d::Error::none);
    std::cout << "Player dialogue completion, fact ownership and atomic failure passed\n";
}
