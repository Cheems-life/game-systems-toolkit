#pragma once

// 解析实现细节：借用第三方 SAX 语法解析，构造有界深度、无分配析构的内部树。
#include <designer/configuration/bundle.hpp>
#include <nlohmann/json.hpp>
#include <variant>
#include <utility>
namespace designer::configuration::detail {
struct Failure { Diagnostic diagnostic; };
// 仅供本加载器使用的树；深度受限，容器析构不另建堆栈、不申请新内存。
struct Value {
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    std::variant<std::monostate, bool, std::uint64_t, std::string, Array, Object> data;
    bool is_object() const { return std::holds_alternative<Object>(data); }
    bool is_array() const { return std::holds_alternative<Array>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_boolean() const { return std::holds_alternative<bool>(data); }
    bool is_number_unsigned() const { return std::holds_alternative<std::uint64_t>(data); }
    template<class T> const T& get() const { return std::get<T>(data); }
    const Value& at(const char* key) const { return get<Object>().at(key); }
    const Value& at(std::size_t index) const { return get<Array>().at(index); }
    bool contains(const char* key) const { return get<Object>().count(key) != 0; }
    std::size_t size() const { return is_array() ? get<Array>().size() : get<Object>().size(); }
};
class Sax final : public nlohmann::json_sax<nlohmann::json> {
public:
    explicit Sax(std::string_view source) : source_(source) {}
    Value root;
    bool null() override { next(); return true; }
    bool boolean(bool b) override { next().data = b; return true; }
    // 非正整数/浮点数保留为非 uint 类型，结构校验可在准确字段位置报错。
    bool number_integer(number_integer_t) override { next(); return true; }
    bool number_unsigned(number_unsigned_t n) override { next().data = n; return true; }
    bool number_float(number_float_t, const string_t&) override { next(); return true; }
    bool string(string_t& text) override { next().data = std::move(text); return true; }
    bool binary(binary_t&) override { fail(Error::structure, "binary JSON is unsupported"); }
    bool start_object(std::size_t) override { return start(true); }
    bool start_array(std::size_t) override { return start(false); }
    bool key(string_t& key) override {
        auto& frame = frames_.back();
        auto& object = std::get<Value::Object>(frame.node->data);
        const auto inserted = object.emplace(key, Value{});
        if (!inserted.second) fail(Error::duplicate, "duplicate JSON key: " + key);
        frame.pending = &inserted.first->second;
        return true;
    }
    bool end_object() override { frames_.pop_back(); return true; }
    bool end_array() override { frames_.pop_back(); return true; }
    bool parse_error(std::size_t position, const std::string&, const nlohmann::detail::exception& e) override {
        throw Failure{{e.id == 406 ? Error::range : Error::syntax, std::string(source_), "$", {}, e.what(), position}};
    }
private:
    struct Frame { Value* node; Value* pending = nullptr; };
    std::vector<Frame> frames_;
    std::string_view source_;
    [[noreturn]] void fail(Error code, std::string message) const {
        throw Failure{{code, std::string(source_), "$", {}, std::move(message)}};
    }
    Value& next() {
        if (frames_.empty()) return root;
        auto& frame = frames_.back();
        if (frame.node->is_array()) {
            auto& array = std::get<Value::Array>(frame.node->data);
            array.emplace_back(); return array.back();
        }
        auto* result = frame.pending;
        frame.pending = nullptr;
        return *result; // 语法解析器保证对象键和值交替出现。
    }
    bool start(bool object) {
        if (frames_.size() >= max_depth) fail(Error::limit, "JSON nesting exceeds 32 levels");
        auto& value = next();
        if (object) value.data = Value::Object{}; else value.data = Value::Array{};
        frames_.push_back({&value}); return true;
    }
};
} // namespace designer::configuration::detail
