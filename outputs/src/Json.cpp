#include "pacemaker/Json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pacemaker {

namespace {
const Json kNull;

void escape(const std::string& s, std::string& out)
{
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
            else out += (char) c;
        }
    }
    out += '"';
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    explicit Parser(const std::string& t) : s(t) {}

    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at " + std::to_string(i); return false; }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool lit(const char* w) { size_t n = std::char_traits<char>::length(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }

    static void utf8(unsigned cp, std::string& o)
    {
        if (cp < 0x80) o += (char) cp;
        else if (cp < 0x800) { o += (char) (0xC0 | (cp >> 6)); o += (char) (0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += (char) (0xE0 | (cp >> 12)); o += (char) (0x80 | ((cp >> 6) & 0x3F)); o += (char) (0x80 | (cp & 0x3F)); }
        else { o += (char) (0xF0 | (cp >> 18)); o += (char) (0x80 | ((cp >> 12) & 0x3F)); o += (char) (0x80 | ((cp >> 6) & 0x3F)); o += (char) (0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned& v)
    {
        if (i + 4 > s.size()) return fail("bad \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned) (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned) (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned) (c - 'A' + 10);
            else return fail("bad hex digit");
        }
        return true;
    }
    bool str(std::string& o)
    {
        ++i; // opening quote
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if ((unsigned char) c < 0x20) return fail("control character in string");
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) break;
            const char e = s[i++];
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
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00) {
                    unsigned lo = 0;
                    if (s.compare(i, 2, "\\u") != 0) return fail("lone surrogate");
                    i += 2;
                    if (!hex4(lo)) return false;
                    if (lo < 0xDC00 || lo > 0xDFFF) return fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) return fail("lone surrogate");
                utf8(cp, o);
                break;
            }
            default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Json& out, int depth)
    {
        if (depth > 64) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            ++i; Json::Object o; ws();
            if (i < s.size() && s[i] == '}') { ++i; out = Json(std::move(o)); return true; }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') return fail("expected key");
                std::string k; if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Json v; if (!value(v, depth + 1)) return false;
                o[k] = std::move(v);
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return fail("expected ',' or '}'");
            }
            out = Json(std::move(o)); return true;
        }
        if (c == '[') {
            ++i; Json::Array a; ws();
            if (i < s.size() && s[i] == ']') { ++i; out = Json(std::move(a)); return true; }
            for (;;) {
                Json v; if (!value(v, depth + 1)) return false;
                a.push_back(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return fail("expected ',' or ']'");
            }
            out = Json(std::move(a)); return true;
        }
        if (c == '"') { std::string t; if (!str(t)) return false; out = Json(std::move(t)); return true; }
        if (lit("true")) { out = Json(true); return true; }
        if (lit("false")) { out = Json(false); return true; }
        if (lit("null")) { out = Json(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* b = s.c_str() + i; char* e = nullptr;
            const double d = std::strtod(b, &e);
            if (e == b || !std::isfinite(d)) return fail("bad number");
            i += (size_t) (e - b); out = Json(d); return true;
        }
        return fail("unexpected character");
    }
};

void write(const Json& j, std::string& o)
{
    switch (j.type()) {
    case Json::Type::Null: o += "null"; break;
    case Json::Type::Bool: o += j.asBool() ? "true" : "false"; break;
    case Json::Type::Number: {
        const double d = j.asNumber();
        if (!std::isfinite(d)) { o += "null"; break; }
        char b[40];
        if (d == std::floor(d) && std::fabs(d) < 1e15) std::snprintf(b, sizeof b, "%.0f", d);
        else std::snprintf(b, sizeof b, "%.6g", d);
        o += b; break;
    }
    case Json::Type::String: escape(j.asString(), o); break;
    case Json::Type::Array: {
        o += '['; bool first = true;
        for (const auto& v : j.items()) { if (!first) o += ','; first = false; write(v, o); }
        o += ']'; break;
    }
    case Json::Type::Object: {
        o += '{'; bool first = true;
        for (const auto& kv : j.members()) { if (!first) o += ','; first = false; escape(kv.first, o); o += ':'; write(kv.second, o); }
        o += '}'; break;
    }
    }
}
} // namespace

const Json& Json::get(const std::string& key) const
{
    if (!isObject()) return kNull;
    auto it = o_.find(key);
    return it == o_.end() ? kNull : it->second;
}
Json& Json::operator[](const std::string& key)
{
    if (!isObject()) { *this = Json(Object {}); }
    return o_[key];
}
void Json::push(Json v)
{
    if (!isArray()) { *this = Json(Array {}); }
    a_.push_back(std::move(v));
}
std::string Json::dump() const { std::string o; write(*this, o); return o; }

bool Json::parse(const std::string& text, Json& out, std::string* error)
{
    Parser p(text);
    Json v;
    bool ok = p.value(v, 0);
    if (ok) { p.ws(); if (p.i != text.size()) ok = p.fail("trailing characters"); }
    if (!ok) { if (error) *error = p.err; return false; }
    out = std::move(v);
    return true;
}

} // namespace pacemaker
