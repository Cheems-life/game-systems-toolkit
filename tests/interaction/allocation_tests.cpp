// 交互异常测试：逐点持续分配失败，独立簿、跨任务提交及 v3 编解码/恢复均不能半提交。
#include "test_support.hpp"
#include <new>
#include <fstream>
#include <iterator>
namespace { long remaining=-1; }
void* operator new(std::size_t size) {
    if(remaining==0) throw std::bad_alloc();
    if(remaining>0) --remaining;
    if(void* p=std::malloc(size==0?1:size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);} void operator delete(void* p,std::size_t) noexcept {std::free(p);}
template<class Create,class Operation> void sweep(Create create,Operation operation) {
    bool done=false;
    for(long n=0;n<6000;++n) {
        auto p=create(); const auto before=p->snapshot(); remaining=n;
        try {const bool ok=operation(*p); remaining=-1; CHECK(ok); done=true;}
        catch(const std::bad_alloc&) {remaining=-1; CHECK(equal(before,p->snapshot()));}
        if(done) break;
    }
    CHECK(done);
}
int main() {
    auto create=[] {auto p=interaction_player(); CHECK(p->accept_quest(1,{5,10})==quest::Error::none); return p;};
    sweep(create,[](auto& p) {return p.execute_interaction({1,7}).error==interact::Error::none;});
    auto source=create(); CHECK(source->execute_interaction({1,7}).error==interact::Error::none);
    const auto snapshot=source->snapshot();
    sweep(create,[&](auto& p) {return p.restore(snapshot)==quest::Error::none;});
    std::string encoded; CHECK(player_api::encode_archive(snapshot,encoded)==player_api::ArchiveError::none);
    for(bool encode:{true,false}) {
        bool done=false;
        for(long n=0;n<6000;++n) {
            std::string output="keep"; player_api::PlayerSnapshot target; const auto before=target; remaining=n;
            try {auto e=encode?player_api::encode_archive(snapshot,output):player_api::decode_archive(encoded,target); remaining=-1; CHECK(e==player_api::ArchiveError::none); CHECK(encode?output==encoded:equal(target,snapshot)); done=true;}
            catch(const std::bad_alloc&) {remaining=-1; CHECK(output=="keep" && equal(before,target));}
            if(done) break;
        }
        CHECK(done);
    }
    bool done=false; const auto catalog=interaction_catalog();
    for(long n=0;n<6000;++n) {
        interact::Book book; remaining=n;
        try {const auto r=book.execute_interaction(catalog,{1,7},1); remaining=-1; CHECK(r.error==interact::Error::none); done=true;}
        catch(const std::bad_alloc&) {remaining=-1; CHECK(book.snapshot().empty());}
        if(done) break;
    }
    CHECK(done);
    done=false;
    for(long n=0;n<6000;++n) {
        interact::Book book; CHECK(book.execute_interaction(catalog,{99,8},1).error==interact::Error::none); const auto before=book.snapshot(); remaining=n;
        try {const auto e=book.restore(snapshot.interactions); remaining=-1; CHECK(e==interact::Error::none); done=true;}
        catch(const std::bad_alloc&) {remaining=-1; CHECK(book.snapshot()==before);}
        if(done) break;
    }
    CHECK(done);
    std::ifstream stream(DESIGNER_INTERACTION_FIXTURE); CHECK(stream.good());
    const std::string json{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
    const auto registry=quest::default_objectives(); done=false;
    for(long n=0;n<10000;++n) {
        remaining=n;
        try {const auto result=designer::configuration::decode(json,registry); remaining=-1; CHECK(result.bundle && result.bundle->interactions().find(7)); done=true;}
        catch(const std::bad_alloc&) {remaining=-1;}
        if(done) break;
    }
    CHECK(done); std::cout << "Interaction allocation failure sweeps passed\n";
}
