// 交互核心测试：无任务/玩家依赖，验证单次、重复、请求身份、模式冻结、容量与恢复。
#include <designer/interaction/interaction.hpp>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace i = designer::interaction;
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed " << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
i::Catalog catalog() { return i::Catalog({{7,1,true,i::Mode::once,200},{8,1,true,i::Mode::repeatable,300},{9,1,false,i::Mode::once,200}}); }
int main() {
    for (auto d : std::vector<i::Definition>{{0,1,true,i::Mode::once,1},{1,0,true,i::Mode::once,1},{1,1,true,i::Mode::once,0},{1,1,true,static_cast<i::Mode>(99),1}}) {
        bool threw=false; try { i::Catalog bad({d}); } catch(const std::invalid_argument&) { threw=true; } CHECK(threw);
    }
    bool threw=false; try { i::Catalog bad({{1,1,true,i::Mode::once,1},{1,1,true,i::Mode::once,1}}); } catch(const std::invalid_argument&) { threw=true; } CHECK(threw);
    threw=false; try { i::Book bad(0); } catch(const std::invalid_argument&) { threw=true; } CHECK(threw);
    threw=false; try { i::Catalog bad(std::vector<i::Definition>(i::max_points+1)); } catch(const std::invalid_argument&) { threw=true; } CHECK(threw);
    i::Book book; auto cfg=catalog();
    CHECK(book.query(cfg,7).allowed && !book.query(cfg,9).allowed);
    CHECK(book.execute(cfg,{1,9},1).error==i::Error::disabled && book.snapshot().empty());
    CHECK(book.execute(cfg,{0,7},1).error==i::Error::invalid_request);
    CHECK(book.execute(cfg,{1,0},1).error==i::Error::invalid_request);
    CHECK(book.execute(cfg,{1,99},1).error==i::Error::unknown_point);
    CHECK(book.execute(cfg,{1,7},0).error==i::Error::invalid_event);
    const auto first=book.execute(cfg,{1,7},10); CHECK(first.error==i::Error::none && first.receipt->event_id==10);
    CHECK(book.execute(cfg,{1,7},999).replayed);
    CHECK(book.execute(cfg,{1,8},11).error==i::Error::request_conflict);
    CHECK(book.execute(cfg,{2,7},11).error==i::Error::already_completed);
    CHECK(book.query(cfg,7).reason==i::Error::already_completed);
    CHECK(book.execute(cfg,{2,8},10).error==i::Error::invalid_event);
    CHECK(book.execute(cfg,{2,8},11).error==i::Error::none);
    CHECK(book.execute(cfg,{3,8},20).error==i::Error::none);
    auto changed=i::Catalog({{7,2,true,i::Mode::repeatable,999},{8,2,true,i::Mode::once,999}});
    CHECK(book.execute(changed,{4,7},21).error==i::Error::already_completed);
    CHECK(book.execute(changed,{4,8},21).error==i::Error::configuration_conflict);
    changed=i::Catalog({{8,2,true,i::Mode::repeatable,999}});
    CHECK(book.execute(changed,{4,8},21).receipt->fact_type==999);
    CHECK(book.execute(changed,{2,8},0).receipt->fact_type==300);
    const auto frozen=book.snapshot(); i::Catalog removed({});
    CHECK(book.execute(removed,{1,7},0).replayed && book.snapshot()==frozen);
    i::Book restored; CHECK(restored.restore(frozen)==i::Error::none && restored.snapshot()==frozen);
    CHECK(restored.execute(removed,{1,7},0).replayed && restored.query(cfg,7).reason==i::Error::already_completed);
    auto reject=[&](i::Snapshot s) { CHECK(restored.restore(s)==i::Error::invalid_snapshot && restored.snapshot()==frozen); };
    auto bad=frozen; bad[1].request.request_id=1; reject(bad);
    bad=frozen; bad[1].event_id=bad[0].event_id; reject(bad);
    bad=frozen; bad[1].request.point=7; reject(bad);
    bad=frozen; bad[1].mode=i::Mode::once; reject(bad);
    bad=frozen; bad[0].fact_type=0; reject(bad);
    bad=frozen; bad[0].version=0; reject(bad);
    i::Book full(1); CHECK(full.execute(cfg,{1,8},1).error==i::Error::none);
    CHECK(full.query(cfg,8).reason==i::Error::history_full && full.execute(cfg,{2,8},2).error==i::Error::history_full);
    CHECK(full.execute(removed,{1,8},0).replayed);
    CHECK(full.restore(frozen)==i::Error::invalid_snapshot);
    std::cout << "Independent inspect interactions, sticky lifecycle and replay passed\n";
}
