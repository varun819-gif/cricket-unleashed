// Minimal JSON for controller messages. Strict and bounded: messages come
// from phones on the local network, so input is untrusted (size and depth
// limits, no trailing garbage, valid UTF-8 escapes only).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cricket::ctl {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json Null() { return Json(); }
    static Json Bool(bool b);
    static Json Number(double n);
    static Json String(std::string s);
    static Json Array();
    static Json Object();

    // Parses `text`. Returns nullopt on any error (message in *err).
    static std::optional<Json> Parse(const std::string& text, std::string* err = nullptr, size_t maxDepth = 16);

    Type GetType() const { return type_; }
    bool IsObject() const { return type_ == Type::Object; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsString() const { return type_ == Type::String; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsBool() const { return type_ == Type::Bool; }

    // Object access (nullptr if missing or not an object).
    const Json* Get(const std::string& key) const;
    std::string Str(const std::string& key, const std::string& def = "") const;
    // Integer field; `def` if missing, not a number, not whole, or out of range.
    int64_t Int(const std::string& key, int64_t def = 0) const;
    double Num(const std::string& key, double def = 0) const;
    bool Flag(const std::string& key, bool def = false) const;

    const std::string& AsString() const { return str_; }
    double AsNumber() const { return num_; }
    bool AsBool() const { return bool_; }
    const std::vector<Json>& Items() const { return arr_; }
    const std::vector<std::pair<std::string, Json>>& Members() const { return obj_; }

    // Builders (keep insertion order, so output is deterministic).
    Json& Set(const std::string& key, Json v);
    Json& Set(const std::string& key, const std::string& v) { return Set(key, String(v)); }
    Json& Set(const std::string& key, const char* v) { return Set(key, String(v)); }
    Json& Set(const std::string& key, int v) { return Set(key, Number(v)); }
    Json& Set(const std::string& key, int64_t v) { return Set(key, Number(static_cast<double>(v))); }
    Json& Set(const std::string& key, double v) { return Set(key, Number(v)); }
    Json& Set(const std::string& key, bool v) { return Set(key, Bool(v)); }
    Json& Push(Json v);

    std::string Dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

}  // namespace cricket::ctl
