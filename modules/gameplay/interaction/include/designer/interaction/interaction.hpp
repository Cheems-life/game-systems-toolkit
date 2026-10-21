#pragma once
// 每玩家检查交互：独立于任务、经济和引擎；这里只记录成功与固定事实效果。
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>
namespace designer::interaction {
using PointId = std::uint32_t;
using RequestId = std::uint64_t;
using EventId = std::uint64_t;
inline constexpr std::size_t max_points = 4096, max_history = 4096;
enum class Mode : std::uint32_t { once = 1, repeatable = 2 };
struct Definition { PointId id; std::uint32_t version; bool enabled; Mode mode; std::uint32_t fact_type; };
class Catalog {
public:
    explicit Catalog(std::vector<Definition> definitions);
    [[nodiscard]] const Definition* find(PointId id) const noexcept;
    [[nodiscard]] const std::map<PointId, Definition>& entries() const noexcept { return definitions_; }
    void swap(Catalog& other) noexcept { definitions_.swap(other.definitions_); }
private:
    std::map<PointId, Definition> definitions_;
};
struct Request { RequestId request_id; PointId point; };
inline bool operator==(const Request& a, const Request& b) noexcept { return a.request_id == b.request_id && a.point == b.point; }
struct Receipt { Request request; std::uint32_t version; Mode mode; std::uint32_t fact_type; EventId event_id; };
inline bool operator==(const Receipt& a, const Receipt& b) noexcept {
    return a.request == b.request && a.version == b.version && a.mode == b.mode && a.fact_type == b.fact_type && a.event_id == b.event_id;
}
using Snapshot = std::vector<Receipt>; // 严格按 event_id 排序，允许间隔；不依赖当前目录恢复。
enum class Error { none, invalid_request, unknown_point, disabled, already_completed, configuration_conflict,
                   request_conflict, history_full, invalid_event, event_id_exhausted, fact_rejected, invalid_snapshot };
[[nodiscard]] const char* error_name(Error) noexcept;
struct View { bool allowed; Error reason; };
struct Result { Error error = Error::none; std::optional<Receipt> receipt = std::nullopt; bool replayed = false; };
// 单线程；查询不替代可信端的距离、视线、权限等检查。
class Book {
public:
    explicit Book(std::size_t history_limit = 256);
    [[nodiscard]] View query(const Catalog&, PointId) const noexcept;
    // 指针在成功 execute/restore 后失效；需要跨操作保存时复制 Receipt 或使用 snapshot。
    [[nodiscard]] const Receipt* receipt(RequestId) const noexcept;
    // 独立使用时调用方负责提供可信递增序号；Player 集成时由协调器分配。
    [[nodiscard]] Result execute(const Catalog&, const Request&, EventId);
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] Error restore(const Snapshot&);
private:
    std::map<RequestId, Receipt> records_;
    std::map<PointId, Mode> modes_;
    EventId last_event_ = 0;
    std::size_t history_limit_;
};
} // namespace designer::interaction
