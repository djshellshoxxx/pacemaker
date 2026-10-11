// Minimal JSON implementation (RS-04 section 4: tolerant loading).
#include "pacemaker/core/Json.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pacemaker { namespace core {

namespace {
const Json& nullJson() { static const Json n; return n; }
const std::string& emptyString() { static const std::string s; return s; }

constexpr int kMaxDepth = 256; // guards against stack exhaustion on hostile input

struct Parser {
    explicit Parser(const std::string& text) : s(text) {}
    const std::string& s;
    size_t p = 0;
    std::string err;
    size_t errAt = 0;

    bool fail(const char* msg)
    {
        if (err.empty()) { err = msg; errAt = p; }
        return false;
    }
    void ws()
    {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
    }
    bool lit(const char* word)
    {
        size_t n = 0;
        while (word[n]) {
            if (p + n >= s.size() || s[p + n] != word[n]) return false;
            ++n;
        }
        p += n;
        return true;
    }
    static void utf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80) out += (char) cp;
        else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F));
            out += (char) (0x80 | (cp & 0x3F));
        } else {
            out += (char) (0xF0 | (cp >> 18)); out += (char) (0x80 | ((cp >> 12) & 0x3F));
            out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F));
        }
    }
    bool hex4(uint32_t& v)
    {
        if (p + 4 > s.size()) return fail("truncated \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t) (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t) (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t) (c - 'A' + 10);
            else { --p; return fail("bad hex digit in \\u escape"); }
        }
        return true;
    }
    bool string(std::string& out)
    {
        if (p >= s.size() || s[p] != '"') return fail("expected string");
        ++p;
        while (true) {
            if (p >= s.size()) return fail("unterminated string");
            const char c = s[p];
            if (c == '"') { ++p; return true; }
            if ((unsigned char) c < 0x20) return fail("control character in string");
            if (c != '\\') { out += c; ++p; continue; }
            ++p;
            if (p >= s.size()) return fail("unterminated escape");
            const char e = s[p++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 1 < s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        p += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { utf8(out, 0xFFFD); cp = lo; }
                    }
                    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
                    utf8(out, cp);
                    break;
                }
                default: --p; return fail("bad escape");
            }
        }
    }
    bool number(Json& out)
    {
        const size_t start = p;
        if (p < s.size() && s[p] == '-') ++p;
        if (p >= s.size()) return fail("bad number");
        if (s[p] == '0') ++p;
        else if (s[p] >= '1' && s[p] <= '9') { while (p < s.size() && std::isdigit((unsigned char) s[p])) ++p; }
        else return fail("bad number");
        if (p < s.size() && s[p] == '.') {
            ++p;
            if (p >= s.size() || !std::isdigit((unsigned char) s[p])) return fail("bad fraction");
            while (p < s.size() && std::isdigit((unsigned char) s[p])) ++p;
        }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
            ++p;
            if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p;
            if (p >= s.size() || !std::isdigit((unsigned char) s[p])) return fail("bad exponent");
            while (p < s.size() && std::isdigit((unsigned char) s[p])) ++p;
        }
        const std::string tok = s.substr(start, p - start);
        double v = std::strtod(tok.c_str(), nullptr);
        if (!std::isfinite(v)) v = 0.0;
        out = Json(v);
        return true;
    }
    bool value(Json& out, int depth)
    {
        if (depth > kMaxDepth) return fail("nesting too deep");
        ws();
        if (p >= s.size()) return fail("unexpected end of input");
        const char c = s[p];
        if (c == '{') {
            ++p;
            out = Json::object();
            ws();
            if (p < s.size() && s[p] == '}') { ++p; return true; }
            while (true) {
                ws();
                std::string key;
                if (!string(key)) return false;
                ws();
                if (p >= s.size() || s[p] != ':') return fail("expected ':'");
                ++p;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.set(key, std::move(v));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == '}') { ++p; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++p;
            out = Json::array();
            ws();
            if (p < s.size() && s[p] == ']') { ++p; return true; }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.push(std::move(v));
                ws();
                if (p < s.size() && s[p] == ',') { ++p; continue; }
                if (p < s.size() && s[p] == ']') { ++p; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string str; if (!string(str)) return false; out = Json(std::move(str)); return true; }
        if (lit("true")) { out = Json(true); return true; }
        if (lit("false")) { out = Json(false); return true; }
        if (lit("null")) { out = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        return fail("unexpected character");
    }
};

void writeString(std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char) c;
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double v)
{
    if (!std::isfinite(v)) { out += "null"; return; }
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char b[32]; std::snprintf(b, sizeof b, "%.0f", v); out += b; return;
    }
    // Shortest round-tripping representation.
    char b[40];
    for (int prec = 15; prec <= 17; ++prec) {
        std::snprintf(b, sizeof b, "%.*g", prec, v);
        if (std::strtod(b, nullptr) == v) break;
    }
    out += b;
}
} // namespace

int Json::asInt(int def) const
{
    if (type_ != Type::Number) return def;
    if (num_ > 2147483647.0 || num_ < -2147483648.0) return def;
    return (int) std::lround(num_);
}

const std::string& Json::asString() const { return type_ == Type::String ? str_ : emptyString(); }

size_t Json::size() const
{
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

const Json& Json::operator[](size_t i) const
{
    return (type_ == Type::Array && i < arr_.size()) ? arr_[i] : nullJson();
}

Json& Json::push(Json v)
{
    if (type_ != Type::Array) { *this = array(); }
    arr_.push_back(std::move(v));
    return arr_.back();
}

const Json* Json::find(const std::string& key) const
{
    if (type_ != Type::Object) return nullptr;
    for (const auto& m : obj_) if (m.first == key) return &m.second;
    return nullptr;
}

const Json& Json::operator[](const std::string& key) const
{
    const Json* j = find(key);
    return j ? *j : nullJson();
}

Json& Json::set(const std::string& key, Json v)
{
    if (type_ != Type::Object) { *this = object(); }
    for (auto& m : obj_) if (m.first == key) { m.second = std::move(v); return m.second; }
    obj_.emplace_back(key, std::move(v));
    return obj_.back().second;
}

bool Json::erase(const std::string& key)
{
    for (auto it = obj_.begin(); it != obj_.end(); ++it)
        if (it->first == key) { obj_.erase(it); return true; }
    return false;
}

bool Json::operator==(const Json& o) const
{
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return bool_ == o.bool_;
        case Type::Number: return num_ == o.num_;
        case Type::String: return str_ == o.str_;
        case Type::Array: return arr_ == o.arr_;
        case Type::Object: return obj_ == o.obj_;
    }
    return false;
}

void Json::write(std::string& out, int indent, int depth) const
{
    const bool pretty = indent >= 0;
    auto nl = [&](int d) {
        if (!pretty) return;
        out += '\n';
        out.append((size_t) (indent * d), ' ');
    };
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: writeNumber(out, num_); break;
        case Type::String: writeString(out, str_); break;
        case Type::Array:
            if (arr_.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i) {
                if (i) out += ',';
                nl(depth + 1);
                arr_[i].write(out, indent, depth + 1);
            }
            nl(depth);
            out += ']';
            break;
        case Type::Object:
            if (obj_.empty()) { out += "{}"; break; }
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i) {
                if (i) out += ',';
                nl(depth + 1);
                writeString(out, obj_[i].first);
                out += pretty ? ": " : ":";
                obj_[i].second.write(out, indent, depth + 1);
            }
            nl(depth);
            out += '}';
            break;
    }
}

std::string Json::dump(int indent) const
{
    std::string out;
    write(out, indent, 0);
    return out;
}

Json::ParseResult Json::parse(const std::string& text)
{
    ParseResult r;
    Parser ps(text);
    // Skip a UTF-8 byte order mark.
    if (text.size() >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB
        && (unsigned char) text[2] == 0xBF)
        ps.p = 3;
    Json v;
    bool ok = ps.value(v, 0);
    if (ok) {
        ps.ws();
        if (ps.p != text.size()) ok = ps.fail("trailing characters");
    }
    if (ok) { r.ok = true; r.value = std::move(v); return r; }
    r.error = ps.err;
    r.errorOffset = ps.errAt;
    r.errorLine = 1; r.errorColumn = 1;
    for (size_t i = 0; i < ps.errAt && i < text.size(); ++i) {
        if (text[i] == '\n') { ++r.errorLine; r.errorColumn = 1; } else ++r.errorColumn;
    }
    return r;
}

bool readTextFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeTextFile(const std::string& path, const std::string& text)
{
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return (bool) f;
}

Json::ParseResult loadJsonFile(const std::string& path)
{
    std::string text;
    if (!readTextFile(path, text)) {
        Json::ParseResult r;
        r.error = "cannot read file";
        return r;
    }
    return Json::parse(text);
}

bool saveJsonFile(const std::string& path, const Json& value, int indent)
{
    return writeTextFile(path, value.dump(indent) + "\n");
}

}} // namespace pacemaker::core
