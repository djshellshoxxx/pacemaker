// Minimal JSON value, parser and writer (RS-07 section 4). No exceptions escape parse().
#pragma once
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
    Json(int n) : type_(Type::Number), n_(n) {}
    Json(long n) : type_(Type::Number), n_((double) n) {}
    Json(long long n) : type_(Type::Number), n_((double) n) {}
    Json(double n) : type_(Type::Number), n_(n) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
    Json(Array a) : type_(Type::Array), a_(std::move(a)) {}
    Json(Object o) : type_(Type::Object), o_(std::move(o)) {}

    static Json array() { return Json(Array {}); }
    static Json object() { return Json(Object {}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool d = false) const { return isBool() ? b_ : d; }
    double asNumber(double d = 0.0) const { return isNumber() ? n_ : d; }
    int asInt(int d = 0) const { return isNumber() ? (int) n_ : d; }
    std::string asString(const std::string& d = {}) const { return isString() ? s_ : d; }
    const Array& items() const { return a_; }
    const Object& members() const { return o_; }

    // Object access. get() returns a shared null for a missing key or a non-object.
    const Json& get(const std::string& key) const;
    bool has(const std::string& key) const { return isObject() && o_.count(key) != 0; }
    Json& operator[](const std::string& key);   // converts null to object
    void push(Json v);                          // converts null to array

    std::string dump() const;
    // Returns false and fills error on malformed input. Depth limited to 64.
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    Array a_;
    Object o_;
};

} // namespace pacemaker
