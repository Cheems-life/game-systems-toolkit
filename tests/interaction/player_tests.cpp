// 玩家交互测试：自动事实序号、接取边界、任务失败不消耗、重复/一次及既有资产操作保留交互状态。
#include "test_support.hpp"
#include <limits>
int main() {
    auto p=interaction_player();
    CHECK(p->accept_quest(1,{5,100})==quest::Error::none);
    CHECK(p->check_interaction_availability(7).allowed && !p->check_interaction_availability(9).allowed);
    CHECK(p->execute_interaction({1,9}).error==interact::Error::disabled && p->interactions().snapshot().empty());
    const auto first=p->execute_interaction({1,8}); CHECK(first.error==interact::Error::none && first.receipt->event_id==101);
    CHECK(p->quests().find_accepted_quest(1)->progress[0]==1);
    CHECK(p->execute_interaction({2,7}).error==interact::Error::none && p->query_quest_status(1,{5,102}).status==quest::Status::ready);
    const auto before=p->snapshot();
    CHECK(p->execute_interaction({1,8}).replayed && equal(before,p->snapshot()));
    CHECK(p->execute_interaction({1,7}).error==interact::Error::request_conflict);
    CHECK(p->execute_interaction({3,7}).error==interact::Error::already_completed);
    CHECK(p->purchase_product(products(), {1,100,1,1}).error==commerce::Error::none); // 经济请求是独立身份域。
    CHECK(p->claim_quest_reward({2,1,1}).error==quest::Error::none);
    CHECK(p->interactions().snapshot()==before.interactions);
    CHECK(p->observe_gameplay_fact({200,999,1,1}).error==quest::Error::none);
    CHECK(p->execute_interaction({3,8}).receipt->event_id==201);
    p->replace_interactions(interact::Catalog({}));
    CHECK(p->execute_interaction({1,8}).replayed && p->execute_interaction({4,8}).error==interact::Error::unknown_point);
    auto full=interaction_player(1); CHECK(full->observe_gameplay_fact({1,999,1,1}).error==quest::Error::none);
    const auto old=full->snapshot(); const auto rejected=full->execute_interaction({1,7});
    CHECK(rejected.error==interact::Error::fact_rejected && rejected.quest_error==quest::Error::event_history_full);
    CHECK(equal(old,full->snapshot()) && full->check_interaction_availability(7).allowed); // 本地可用提示不是任务提交承诺。
    auto maxed=interaction_player(); CHECK(maxed->accept_quest(1,{5,std::numeric_limits<quest::EventId>::max()})==quest::Error::none);
    const auto exhausted=maxed->snapshot();
    CHECK(maxed->check_interaction_availability(7).reason==interact::Error::event_id_exhausted);
    CHECK(maxed->execute_interaction({1,7}).error==interact::Error::event_id_exhausted && equal(exhausted,maxed->snapshot()));
    auto early=interaction_player(); CHECK(early->execute_interaction({1,7}).error==interact::Error::none);
    CHECK(early->accept_quest(1,{5,1})==quest::Error::none && early->execute_interaction({1,7}).replayed);
    CHECK(early->quests().find_accepted_quest(1)->progress[1]==0 && early->execute_interaction({2,7}).error==interact::Error::already_completed);
    auto limit=interaction_player(256,1); CHECK(limit->execute_interaction({1,8}).error==interact::Error::none);
    CHECK(limit->execute_interaction({2,8}).error==interact::Error::history_full);
    CHECK(limit->execute_interaction({1,8}).replayed);
    // 一次事实匹配 65 目标时失败；用直接目标接口完成并领取一个任务后，原交互请求可重试成功。
    std::vector<quest::QuestDefinition> tasks;
    for (quest::QuestId id=1;id<=65;++id) { auto d=interaction_task(id); d.objectives.erase(d.objectives.begin()); tasks.push_back(d); }
    player_api::Player fanout(quest::QuestCatalog(tasks),quest::default_objectives(),make_economy().clone(),256,256,interaction_catalog());
    for (quest::QuestId id=1;id<=65;++id) CHECK(fanout.accept_quest(id,{5,0})==quest::Error::none);
    const auto untouched=fanout.snapshot(); CHECK(fanout.execute_interaction({42,7}).error==interact::Error::fact_rejected);
    CHECK(equal(untouched,fanout.snapshot()));
    CHECK(fanout.apply_quest_progress_event({1,{{1,20,1}}}).error==quest::Error::none && fanout.claim_quest_reward({1,1,1}).error==quest::Error::none);
    const auto retried=fanout.execute_interaction({42,7}); CHECK(retried.error==interact::Error::none && retried.receipt->event_id==2);
    CHECK(fanout.snapshot().quests.events.back().updates.size()==64 && fanout.execute_interaction({42,7}).replayed);
    std::cout << "Atomic interaction-to-quest delivery and retry passed\n";
}
