// Minimal JSON value, parser and writer (RFC 8259 subset: no surrogate-pair escapes beyond \uXXXX BMP).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pacemaker {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(int v) : type_(Type::Number), n_(v) {}
    Json(int64_t v) : type_(Type::Number), n_((double) v) {}
    Json(double v) : type_(Type::Number), n_(v) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isBool() const { return type_ == Type::Bool; }

    // Tolerant getters: return the fallback on a missing key or type mismatch.
    bool getBool(bool fb = false) const { return type_ == Type::Bool ? b_ : (type_ == Type::Number ? n_ != 0 : fb); }
    double getNumber(double fb = 0) const { return type_ == Type::Number ? n_ : (type_ == Type::Bool ? (b_ ? 1 : 0) : fb); }
    int getInt(int fb = 0) const { return type_ == Type::Number ? (int) (n_ < 0 ? n_ - 0.5 : n_ + 0.5) : fb; }
    std::string getString(const std::string& fb = {}) const { return type_ == Type::String ? s_ : fb; }
    const Array& items() const { static const Array e; return type_ == Type::Array ? a_ : e; }
    const Object& members() const { static const Object e; return type_ == Type::Object ? o_ : e; }

    const Json& operator[](const std::string& k) const
    {
        static const Json nul;
        if (type_ != Type::Object) return nul;
        auto it = o_.find(k);
        return it == o_.end() ? nul : it->second;
    }
    bool has(const std::string& k) const { return type_ == Type::Object && o_.count(k) > 0; }
    Json& set(const std::string& k, Json v) { if (type_ != Type::Object) *this = object(); o_[k] = std::move(v); return *this; }
    Json& push(Json v) { if (type_ != Type::Array) *this = array(); a_.push_back(std::move(v)); return *this; }

    std::string dump(int indent = -1) const;
    // Returns false (and leaves out Null) on a syntax error.
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    void write(std::string& o, int indent, int depth) const;
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    Array a_;
    Object o_;
};

} // namespace pacemaker
