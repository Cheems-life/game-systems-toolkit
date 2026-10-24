#pragma once
// 引擎无关对话流程：定义与正文分离，命令只走一步，活动会话固定旧图。
#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace designer::dialogue
{
    using DialogueId = std::uint32_t;
    using NodeId = std::uint32_t;
    using OptionId = std::uint32_t;
    using RequestId = std::uint64_t;
    using SessionId = std::uint64_t;
    using EventId = std::uint64_t;
    inline constexpr std::size_t max_dialogues = 128, max_nodes = 128, max_options = 8, max_sessions = 16,
                                 max_history = 1024, max_key_bytes = 96;
    enum class NodeKind : std::uint32_t
    {
        text = 1,
        choice = 2,
        end = 3
    };
    struct Option
    {
        OptionId id;
        std::string text_key;
        NodeId next;
    };
    inline bool operator==(const Option &a, const Option &b)
    {
        return a.id == b.id && a.text_key == b.text_key && a.next == b.next;
    }
    struct Node
    {
        NodeId id;
        NodeKind kind;
        std::uint32_t speaker = 0;
        std::string text_key = {};
        NodeId next = 0;
        std::vector<Option> options = {};
    };
    inline bool operator==(const Node &a, const Node &b)
    {
        return a.id == b.id && a.kind == b.kind && a.speaker == b.speaker && a.text_key == b.text_key &&
               a.next == b.next && a.options == b.options;
    }
    // 流程只保存文本键；speaker 是接入方定义的角色 ID，不持有场景对象。
    struct Definition
    {
        DialogueId id;
        std::uint32_t version;
        NodeId entry;
        std::uint32_t completion_fact_type;
        std::vector<Node> nodes;
    };
    inline bool operator==(const Definition &a, const Definition &b)
    {
        return a.id == b.id && a.version == b.version && a.entry == b.entry &&
               a.completion_fact_type == b.completion_fact_type && a.nodes == b.nodes;
    }
    [[nodiscard]] bool valid_text_key(const std::string &) noexcept;
    // 返回规范化且完整校验的图；非法定义抛 invalid_argument，分配失败向上传递。
    [[nodiscard]] Definition validate_dialogue_definition(Definition);
    // 输入图须先经 validate_dialogue_definition 规范化（节点按 ID 排序）。
    [[nodiscard]] const Node *find_dialogue_node(const Definition &, NodeId) noexcept;
    class Book;
    class Catalog
    {
      public:
        explicit Catalog(std::vector<Definition> definitions);
        [[nodiscard]] const Definition *find(DialogueId) const noexcept;
        void replace_dialogue_definition(Definition); // 同 ID 改内容须提升版本；成功后只替换当前目录。
        bool remove_dialogue_definition(DialogueId) noexcept;
        void swap(Catalog &other) noexcept
        {
            definitions_.swap(other.definitions_);
        }

      private:
        friend class Book;
        std::map<DialogueId, std::shared_ptr<const Definition>> definitions_;
    };
    enum class Action : std::uint32_t
    {
        start = 1,
        advance = 2,
        choose = 3,
        finish = 4,
        cancel = 5
    };
    enum class Status : std::uint32_t
    {
        active = 1,
        completed = 2,
        cancelled = 3
    };
    // start 只填写 dialogue；其他动作填写 session、期望节点和修订号，choose 额外填写 option。
    // 所有未使用字段必须为 0，同一 request_id 必须始终使用完全相同的命令。
    struct Command
    {
        RequestId request_id;
        Action action;
        DialogueId dialogue = 0;
        SessionId session = 0;
        NodeId expected_node = 0;
        std::uint64_t expected_revision = 0;
        OptionId option = 0;
    };
    inline bool operator==(const Command &a, const Command &b)
    {
        return a.request_id == b.request_id && a.action == b.action && a.dialogue == b.dialogue &&
               a.session == b.session && a.expected_node == b.expected_node &&
               a.expected_revision == b.expected_revision && a.option == b.option;
    }
    [[nodiscard]] bool valid_dialogue_command(const Command &) noexcept;
    struct Receipt
    {
        Command command;
        std::uint64_t sequence;
        SessionId session;
        NodeId node;
        Status status;
        std::uint64_t revision;
        EventId completion_event = 0;
    };
    inline bool operator==(const Receipt &a, const Receipt &b)
    {
        return a.command == b.command && a.sequence == b.sequence && a.session == b.session &&
               a.node == b.node && a.status == b.status && a.revision == b.revision &&
               a.completion_event == b.completion_event;
    }
    struct SessionSnapshot
    {
        SessionId id;
        Definition definition;
        NodeId node;
        Status status;
        std::uint64_t revision;
    };
    inline bool operator==(const SessionSnapshot &a, const SessionSnapshot &b)
    {
        return a.id == b.id && a.definition == b.definition && a.node == b.node && a.status == b.status &&
               a.revision == b.revision;
    }
    struct Snapshot
    {
        std::vector<SessionSnapshot> sessions;
        std::vector<Receipt> commands;
    };
    inline bool operator==(const Snapshot &a, const Snapshot &b)
    {
        return a.sessions == b.sessions && a.commands == b.commands;
    }
    enum class Error
    {
        none,
        invalid_command,
        request_conflict,
        unknown_dialogue,
        unknown_session,
        active_session,
        session_closed,
        stale_view,
        wrong_node_kind,
        unknown_option,
        history_full,
        session_limit,
        invalid_event,
        event_id_exhausted,
        fact_rejected,
        invalid_snapshot
    };
    [[nodiscard]] const char *error_name(Error) noexcept;
    struct Result
    {
        Error error = Error::none;
        std::optional<Receipt> receipt = std::nullopt;
        bool replayed = false;
    };
    // 借用视图：Book 成功修改/恢复或所属 Player 提交后可能失效，不跨操作缓存引用。
    struct SessionView
    {
        SessionId id;
        const Definition &definition;
        const Node &node;
        Status status;
        std::uint64_t revision;
    };
    class Book
    {
      public:
        explicit Book(std::size_t history_limit = 256);
        [[nodiscard]] std::optional<SessionView> query_dialogue_session(SessionId) const noexcept;
        [[nodiscard]] const Receipt *find_dialogue_receipt(RequestId) const noexcept;
        // 独立使用时可信调用方提供递增完成事件 ID；本函数不执行任务、网络或场景回调。
        [[nodiscard]] Result execute_dialogue_command(const Catalog &, const Command &,
                                                      EventId completion_event = 0);
        [[nodiscard]] Snapshot snapshot() const;
        [[nodiscard]] Error restore(const Snapshot &);

      private:
        struct Session
        {
            SessionId id;
            std::shared_ptr<const Definition> definition;
            NodeId node;
            Status status;
            std::uint64_t revision;
        };
        Result perform_command(const Catalog &, const Command &, EventId);
        std::map<SessionId, Session> sessions_;
        std::map<RequestId, Receipt> commands_;
        std::size_t history_limit_;
    };
} // namespace designer::dialogue
