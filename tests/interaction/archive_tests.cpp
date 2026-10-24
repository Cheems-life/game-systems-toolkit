// 交互 v3 兼容读取与当前玩家 v4：交互凭证与原文关联、独立固定样本、截断/篡改/变异、旧版本补空记录。
#include "test_support.hpp"
#include "archive_golden.hpp"
#include <random>
void seal(std::string& bytes) {
    std::uint64_t hash=14695981039346656037ULL;
    for(std::size_t i=0;i<bytes.size()-8;++i) { hash^=static_cast<unsigned char>(bytes[i]); hash*=1099511628211ULL; }
    for(unsigned i=0;i<8;++i) bytes[bytes.size()-8+i]=static_cast<char>((hash>>(i*8))&255);
}
int main() {
    player_api::PlayerSnapshot fixture; CHECK(player_api::decode_archive(interaction_golden,fixture)==player_api::ArchiveError::none);
    CHECK(fixture.interactions.size()==1 && fixture.interactions[0].request.request_id==50 && fixture.quests.events[0].fact->subject==7);
    std::string canonical; CHECK(player_api::encode_archive(fixture,canonical)==player_api::ArchiveError::none && canonical==interaction_golden_v4);
    eco::Wallet wallet(eco::CurrencyCatalog({{1,1000}}));
    auto assets=std::make_unique<eco::Economy>(std::move(wallet),std::vector<eco::Container>{{1,inv::Inventory(1,inv::ItemCatalog({{10,10}}))}});
    player_api::Player golden(commerce::ProductCatalog({}),quest::QuestCatalog({}),quest::default_objectives(),std::move(assets));
    CHECK(golden.restore(fixture)==quest::Error::none && golden.execute_interaction({50,7}).replayed);
    CHECK(golden.execute_interaction({51,7}).error==interact::Error::already_completed);
    auto p=interaction_player(); CHECK(p->accept_quest(1,{5,0})==quest::Error::none);
    CHECK(p->execute_interaction({1,8}).error==interact::Error::none && p->execute_interaction({2,7}).error==interact::Error::none);
    CHECK(p->claim_quest_reward({1,1,1}).error==quest::Error::none);
    const auto original=p->snapshot(); std::string bytes; CHECK(player_api::encode_archive(original,bytes)==player_api::ArchiveError::none);
    player_api::PlayerSnapshot decoded; CHECK(player_api::decode_archive(bytes,decoded)==player_api::ArchiveError::none && equal(decoded,original));
    auto restored=interaction_player(); restored->replace_interactions(interact::Catalog({})); restored->replace_quests(quest::QuestCatalog({}));
    CHECK(restored->restore(decoded)==quest::Error::none && restored->execute_interaction({1,8}).replayed && restored->execute_interaction({2,7}).replayed);
    CHECK(restored->claim_quest_reward({1,1,1}).replayed);
    auto reject=[&](player_api::PlayerSnapshot bad) {
        CHECK(restored->restore(bad)==quest::Error::invalid_snapshot && equal(original,restored->snapshot()));
        std::string keep="keep"; CHECK(player_api::encode_archive(bad,keep)==player_api::ArchiveError::malformed && keep=="keep");
    };
    auto bad=original; bad.interactions[0].fact_type=999; reject(bad);
    bad=original; bad.interactions[0].event_id=99; reject(bad);
    bad=original; bad.quests.events[0].fact.reset(); reject(bad);
    bad=original; bad.interactions[1].request.request_id=1; reject(bad);
    bad=original; bad.interactions[1].event_id=1; reject(bad);
    bad=original; bad.interactions[1].request.point=8; reject(bad); // 同点模式漂移且事实对象不匹配。
    bad=original; bad.interactions[0].mode=static_cast<interact::Mode>(99); reject(bad);
    for(std::size_t n=0;n<bytes.size();++n) { CHECK(player_api::decode_archive(std::string_view(bytes).substr(0,n),decoded)!=player_api::ArchiveError::none); CHECK(equal(decoded,original)); }
    auto corrupt=bytes; corrupt[8]=5; CHECK(player_api::decode_archive(corrupt,decoded)==player_api::ArchiveError::unsupported_version);
    std::mt19937 random(20261022);
    for(int n=0;n<1500;++n) {
        corrupt=bytes; corrupt[random()%corrupt.size()]^=static_cast<char>(1+random()%255); if(n%2==0) seal(corrupt);
        const auto before=decoded; const auto result=player_api::decode_archive(corrupt,decoded);
        if(result!=player_api::ArchiveError::none) CHECK(equal(before,decoded));
        else { CHECK(player_api::encode_archive(decoded,canonical)==player_api::ArchiveError::none); if(corrupt[8]==4) CHECK(canonical==corrupt); }
    }
    // 删除交互归属但保留事实，语法上与外部可信事实无法区分；格式校验不是认证。
    auto external=original; external.interactions.clear(); CHECK(restored->restore(external)==quest::Error::none);
    std::cout << "Interaction v3 archive, owned fact links and mutation safety passed\n";
}
