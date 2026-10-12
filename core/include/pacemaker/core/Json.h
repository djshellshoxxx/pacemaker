// Minimal JSON value, parser and writer for state, presets and settings (RS-04 sections 2-5).
// Objects keep insertion order so saved files diff cleanly. The parser never throws or crashes
// on malformed input; it reports an error message and byte offset instead.
#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace pacemaker { namespace core {

class Json {
public:
    enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
    using Member = std::pair<std::string, Json>;

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    // Any integer type (int, int64_t, size_t, ...) without overload ambiguity.
    template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Json(T v) : type_(Type::Number), num_((double) v) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(const char* s) : type_(Type::String), str_(s ? s : "") {}
    Json(std::string s) : type_(Type::String), str_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Typed reads with defaults: a wrong type returns the default.
    bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
    double asNumber(double def = 0.0) const { return type_ == Type::Number ? num_ : def; }
    int asInt(int def = 0) const;
    const std::string& asString() const;   // empty string when not a string
    std::string asString(const std::string& def) const { return type_ == Type::String ? str_ : def; }

    // Arrays.
    size_t size() const;                   // elements or members; 0 otherwise
    const Json& operator[](size_t i) const; // null value when out of range
    Json& push(Json v);                    // converts null to array
    const std::vector<Json>& items() const { return arr_; }

    // Objects.
    bool has(const std::string& key) const { return find(key) != nullptr; }
    const Json* find(const std::string& key) const;
    const Json& operator[](const std::string& key) const; // null value when missing
    const Json& operator[](const char* key) const { return (*this)[std::string(key)]; }
    Json& set(const std::string& key, Json v); // converts null to object, replaces in place
    bool erase(const std::string& key);
    const std::vector<Member>& members() const { return obj_; }

    // Convenience getters on objects.
    double getNumber(const std::string& key, double def) const { return (*this)[key].asNumber(def); }
    int getInt(const std::string& key, int def) const { return (*this)[key].asInt(def); }
    bool getBool(const std::string& key, bool def) const { return (*this)[key].asBool(def); }
    std::string getString(const std::string& key, const std::string& def) const { return (*this)[key].asString(def); }

    // Serialisation. indent < 0 = compact, otherwise pretty with that many spaces.
    std::string dump(int indent = -1) const;

    struct ParseResult;
    static ParseResult parse(const std::string& text);

    bool operator==(const Json& o) const;
    bool operator!=(const Json& o) const { return !(*this == o); }

private:
    void write(std::string& out, int indent, int depth) const;
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<Member> obj_;
};

struct Json::ParseResult {
    Json value;
    bool ok = false;
    std::string error;
    size_t errorOffset = 0;  // byte offset of the error
    int errorLine = 0, errorColumn = 0; // 1-based
};

// File helpers (std::filesystem/fstream).
bool readTextFile(const std::string& path, std::string& out);
bool writeTextFile(const std::string& path, const std::string& text); // creates parent dirs
Json::ParseResult loadJsonFile(const std::string& path);
bool saveJsonFile(const std::string& path, const Json& value, int indent = 2);

}} // namespace pacemaker::core
