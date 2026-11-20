#include "cricket/controller/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace cricket::ctl {

Json Json::Bool(bool b) {
    Json j;
    j.type_ = Type::Bool;
    j.bool_ = b;
    return j;
}
Json Json::Number(double n) {
    Json j;
    j.type_ = Type::Number;
    j.num_ = std::isfinite(n) ? n : 0;
    return j;
}
Json Json::String(std::string s) {
    Json j;
    j.type_ = Type::String;
    j.str_ = std::move(s);
    return j;
}
Json Json::Array() {
    Json j;
    j.type_ = Type::Array;
    return j;
}
Json Json::Object() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

const Json* Json::Get(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& [k, v] : obj_)
        if (k == key) return &v;
    return nullptr;
}

std::string Json::Str(const std::string& key, const std::string& def) const {
    const Json* v = Get(key);
    return v && v->IsString() ? v->str_ : def;
}

int64_t Json::Int(const std::string& key, int64_t def) const {
    const Json* v = Get(key);
    if (!v || !v->IsNumber()) return def;
    const double n = v->num_;
    if (n != std::floor(n) || n < -9007199254740991.0 || n > 9007199254740991.0) return def;
    return static_cast<int64_t>(n);
}

double Json::Num(const std::string& key, double def) const {
    const Json* v = Get(key);
    return v && v->IsNumber() ? v->num_ : def;
}

bool Json::Flag(const std::string& key, bool def) const {
    const Json* v = Get(key);
    return v && v->IsBool() ? v->bool_ : def;
}

Json& Json::Set(const std::string& key, Json v) {
    type_ = Type::Object;
    for (auto& [k, old] : obj_)
        if (k == key) {
            old = std::move(v);
            return *this;
        }
    obj_.emplace_back(key, std::move(v));
    return *this;
}

Json& Json::Push(Json v) {
    type_ = Type::Array;
    arr_.push_back(std::move(v));
    return *this;
}

namespace {

void DumpString(const std::string& s, std::string& o) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    o += '"';
}

void DumpTo(const Json& j, std::string& o) {
    switch (j.GetType()) {
        case Json::Type::Null: o += "null"; break;
        case Json::Type::Bool: o += j.AsBool() ? "true" : "false"; break;
        case Json::Type::Number: {
            const double n = j.AsNumber();
            char b[32];
            if (n == std::floor(n) && std::fabs(n) < 1e15) std::snprintf(b, sizeof b, "%.0f", n);
            else std::snprintf(b, sizeof b, "%.17g", n);
            o += b;
            break;
        }
        case Json::Type::String: DumpString(j.AsString(), o); break;
        case Json::Type::Array: {
            o += '[';
            bool first = true;
            for (const auto& v : j.Items()) {
                if (!first) o += ',';
                first = false;
                DumpTo(v, o);
            }
            o += ']';
            break;
        }
        case Json::Type::Object: {
            o += '{';
            bool first = true;
            for (const auto& [k, v] : j.Members()) {
                if (!first) o += ',';
                first = false;
                DumpString(k, o);
                o += ':';
                DumpTo(v, o);
            }
            o += '}';
            break;
        }
    }
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    size_t maxDepth;
    std::string err;

    void Ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool Fail(const std::string& e) {
        if (err.empty()) err = e + " at " + std::to_string(i);
        return false;
    }
    bool Lit(const char* w) {
        size_t n = std::char_traits<char>::length(w);
        if (s.compare(i, n, w) != 0) return Fail("unexpected token");
        i += n;
        return true;
    }
    static void Utf8(uint32_t cp, std::string& o) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) {
            o += static_cast<char>(0xC0 | (cp >> 6));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool Hex4(uint32_t* v) {
        if (i + 4 > s.size()) return Fail("bad escape");
        *v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return Fail("bad escape");
            *v = (*v << 4) | static_cast<uint32_t>(d);
        }
        return true;
    }
    bool Str(std::string& o) {
        if (s[i] != '"') return Fail("expected string");
        ++i;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '"') {
                ++i;
                return true;
            }
            if (c < 0x20) return Fail("control character in string");
            if (c != '\\') {
                o += static_cast<char>(c);
                ++i;
                continue;
            }
            if (++i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!Hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t lo = 0;
                        if (s.compare(i, 2, "\\u") != 0) return Fail("lone surrogate");
                        i += 2;
                        if (!Hex4(&lo) || lo < 0xDC00 || lo > 0xDFFF) return Fail("bad surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return Fail("lone surrogate");
                    }
                    Utf8(cp, o);
                    break;
                }
                default: return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }
    bool Num(double& n) {
        size_t start = i;
        if (s[i] == '-') ++i;
        if (i >= s.size() || !(s[i] >= '0' && s[i] <= '9')) return Fail("bad number");
        if (s[i] == '0') ++i;
        else
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (i >= s.size() || !(s[i] >= '0' && s[i] <= '9')) return Fail("bad number");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i >= s.size() || !(s[i] >= '0' && s[i] <= '9')) return Fail("bad number");
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        n = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        if (!std::isfinite(n)) return Fail("number out of range");
        return true;
    }
    bool Value(Json& out, size_t depth) {
        if (depth > maxDepth) return Fail("nested too deeply");
        Ws();
        if (i >= s.size()) return Fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            ++i;
            out = Json::Object();
            Ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            while (true) {
                Ws();
                if (i >= s.size()) return Fail("unexpected end");
                std::string key;
                if (!Str(key)) return false;
                if (out.Get(key)) return Fail("duplicate key");
                Ws();
                if (i >= s.size() || s[i] != ':') return Fail("expected ':'");
                ++i;
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.Set(key, std::move(v));
                Ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    ++i;
                    return true;
                }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i;
            out = Json::Array();
            Ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            while (true) {
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.Push(std::move(v));
                Ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    ++i;
                    return true;
                }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string str;
            if (!Str(str)) return false;
            out = Json::String(std::move(str));
            return true;
        }
        if (c == 't') {
            if (!Lit("true")) return false;
            out = Json::Bool(true);
            return true;
        }
        if (c == 'f') {
            if (!Lit("false")) return false;
            out = Json::Bool(false);
            return true;
        }
        if (c == 'n') {
            if (!Lit("null")) return false;
            out = Json::Null();
            return true;
        }
        double n = 0;
        if (!Num(n)) return false;
        out = Json::Number(n);
        return true;
    }
};

}  // namespace

std::optional<Json> Json::Parse(const std::string& text, std::string* err, size_t maxDepth) {
    Parser p{text, 0, maxDepth, {}};
    Json out;
    if (!p.Value(out, 0)) {
        if (err) *err = p.err;
        return std::nullopt;
    }
    p.Ws();
    if (p.i != text.size()) {
        if (err) *err = "trailing characters at " + std::to_string(p.i);
        return std::nullopt;
    }
    return out;
}

std::string Json::Dump() const {
    std::string o;
    DumpTo(*this, o);
    return o;
}

}  // namespace cricket::ctl
