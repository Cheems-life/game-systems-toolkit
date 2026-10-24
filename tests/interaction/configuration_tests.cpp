// 配置 v3：交互表的严格字段/范围/模式/上限，旧配置补空目录，加载后实际交互。
#include "test_support.hpp"
#include <fstream>
#include <iterator>
namespace cfg=designer::configuration;
std::string replace_text(std::string text,const std::string& from,const std::string& to) {
    const auto pos=text.find(from); CHECK(pos!=std::string::npos); text.replace(pos,from.size(),to); return text;
}
int main() {
    std::ifstream stream(DESIGNER_INTERACTION_FIXTURE); CHECK(stream.good());
    const std::string bytes{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
    const auto loaded=cfg::decode(bytes,quest::default_objectives()); CHECK(loaded.bundle && loaded.bundle->interactions().find(7));
    CHECK(loaded.bundle->interactions().find(8)->mode==interact::Mode::repeatable);
    auto reject=[&](std::string text,cfg::Error code,const std::string& field) {
        const auto result=cfg::decode(text,quest::default_objectives());
        CHECK(!result.bundle && result.error && result.error->code==code && result.error->path==field);
    };
    const std::string base="$.interactions[0].";
    reject(replace_text(bytes,"\"mode\": \"once\"","\"mode\": \"daily\""),cfg::Error::rule,base+"mode");
    reject(replace_text(bytes,"\"enabled\": true","\"enabled\": 1"),cfg::Error::structure,base+"enabled");
    reject(replace_text(bytes,"\"fact_type\": 200\n    }","\"fact_type\": 0\n    }"),cfg::Error::range,base+"fact_type");
    reject(replace_text(bytes,"\"id\": 7,\n      \"version\"","\"id\": 0,\n      \"version\""),cfg::Error::range,base+"id");
    reject(replace_text(bytes,"\"mode\": \"once\",","\"mode\": \"once\", \"extra\": 1,"),cfg::Error::structure,base+"extra");
    reject(replace_text(bytes,"\"id\": 8,\n      \"version\"","\"id\": 7,\n      \"version\""),cfg::Error::duplicate,"$.interactions[1].id");
    const std::string empty=R"({"schema_version":3,"content_version":1,"items":[],"currencies":[],"products":[],"quests":[],"interactions":[]})";
    CHECK(cfg::decode(empty,quest::default_objectives()).bundle);
    reject(replace_text(empty,",\"interactions\":[]",""),cfg::Error::structure,"$.interactions");
    reject(replace_text(empty,"\"schema_version\":3","\"schema_version\":2"),cfg::Error::structure,"$.interactions");
    for(auto version:{1,2}) {
        auto legacy=replace_text(empty,",\"interactions\":[]",""); legacy=replace_text(legacy,"\"schema_version\":3","\"schema_version\":"+std::to_string(version));
        const auto result=cfg::decode(legacy,quest::default_objectives()); CHECK(result.bundle && result.bundle->interactions().entries().empty());
    }
    std::string rows; for(std::size_t n=0;n<=interact::max_points;++n) { if(n) rows+=','; rows+="{}"; }
    reject(replace_text(empty,"\"interactions\":[]","\"interactions\":["+rows+"]"),cfg::Error::limit,"$.interactions");
    const auto& data=*loaded.bundle;
    eco::Wallet wallet(data.currencies());
    auto assets=std::make_unique<eco::Economy>(std::move(wallet),std::vector<eco::Container>{{1,inv::Inventory(16,data.items())}});
    player_api::Player p(data.quests(),data.objectives(),std::move(assets),256,256,data.interactions());
    CHECK(p.accept_quest(1,{5,0})==quest::Error::none && p.execute_interaction({1,8}).error==interact::Error::none && p.execute_interaction({2,7}).error==interact::Error::none);
    CHECK(p.query_quest_status(1,{5,2}).status==quest::Status::ready);
    std::cout << "Interaction configuration v3 and legacy schema compatibility passed\n";
}
