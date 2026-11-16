#include "pacemaker/Json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pacemaker {

namespace {
struct Parser {
    const std::string& t;
    size_t i = 0;
    std::string err;
    int depth = 0;
    explicit Parser(const std::string& s) : t(s) {}

    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at " + std::to_string(i); return false; }
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
    bool lit(const char* s) { size_t n = std::char_traits<char>::length(s); if (t.compare(i, n, s) == 0) { i += n; return true; } return false; }

    bool str(std::string& out)
    {
        if (t[i] != '"') return fail("expected string");
        ++i; out.clear();
        while (i < t.size()) {
            char c = t[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= t.size()) break;
                char e = t[i++];
                switch (e) {
                case '"': out += '"'; break; case '\\': out += '\\'; break; case '/': out += '/'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break; case 'n': out += '\n'; break;
                case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': {
                    if (i + 4 > t.size()) return fail("bad \\u");
                    unsigned cp = (unsigned) std::strtoul(t.substr(i, 4).c_str(), nullptr, 16); i += 4;
                    if (cp < 0x80) out += (char) cp;
                    else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
                    else { out += (char) (0xE0 | (cp >> 12)); out += (char) (0x80 | ((cp >> 6) & 0x3F)); out += (char) (0x80 | (cp & 0x3F)); }
                    break;
                }
                default: return fail("bad escape");
                }
            } else out += c;
        }
        return fail("unterminated string");
    }

    bool value(Json& out)
    {
        ws();
        if (i >= t.size()) return fail("unexpected end");
        if (++depth > 64) return fail("too deep");
        struct D { int& d; ~D() { --d; } } guard { depth };
        char c = t[i];
        if (c == '{') {
            ++i; out = Json::object(); ws();
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            for (;;) {
                ws(); std::string k; if (i >= t.size() || !str(k)) return fail("expected key");
                ws(); if (i >= t.size() || t[i] != ':') return fail("expected ':'"); ++i;
                Json v; if (!value(v)) return false;
                out.set(k, std::move(v)); ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i; out = Json::array(); ws();
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            for (;;) {
                Json v; if (!value(v)) return false;
                out.push(std::move(v)); ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string s; if (!str(s)) return false; out = Json(std::move(s)); return true; }
        if (lit("true")) { out = Json(true); return true; }
        if (lit("false")) { out = Json(false); return true; }
        if (lit("null")) { out = Json(); return true; }
        const char* start = t.c_str() + i; char* end = nullptr;
        double v = std::strtod(start, &end);
        if (end == start) return fail("unexpected character");
        i += (size_t) (end - start); out = Json(v); return true;
    }
};

void escape(std::string& o, const std::string& s)
{
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char) c;
        }
    }
    o += '"';
}
} // namespace

bool Json::parse(const std::string& text, Json& out, std::string* error)
{
    Parser p(text);
    Json v;
    bool ok = p.value(v);
    if (ok) { p.ws(); if (p.i != text.size()) { ok = false; p.fail("trailing characters"); } }
    if (!ok) { out = Json(); if (error) *error = p.err; return false; }
    out = std::move(v);
    return true;
}

void Json::write(std::string& o, int indent, int depth) const
{
    auto nl = [&](int d) { if (indent >= 0) { o += '\n'; o.append((size_t) (indent * d), ' '); } };
    switch (type_) {
    case Type::Null: o += "null"; break;
    case Type::Bool: o += b_ ? "true" : "false"; break;
    case Type::Number: {
        if (!std::isfinite(n_)) { o += "null"; break; }
        char b[40];
        if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(b, sizeof b, "%.0f", n_);
        else std::snprintf(b, sizeof b, "%.10g", n_);
        o += b; break;
    }
    case Type::String: escape(o, s_); break;
    case Type::Array:
        o += '[';
        for (size_t k = 0; k < a_.size(); ++k) { if (k) o += ','; nl(depth + 1); a_[k].write(o, indent, depth + 1); }
        if (!a_.empty()) nl(depth);
        o += ']'; break;
    case Type::Object: {
        o += '{'; size_t k = 0;
        for (const auto& kv : o_) { if (k++) o += ','; nl(depth + 1); escape(o, kv.first); o += indent >= 0 ? ": " : ":"; kv.second.write(o, indent, depth + 1); }
        if (!o_.empty()) nl(depth);
        o += '}'; break;
    }
    }
}

std::string Json::dump(int indent) const { std::string s; write(s, indent, 0); return s; }

} // namespace pacemaker
