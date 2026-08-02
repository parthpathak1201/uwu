#pragma once

#include "common.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>

namespace uwu::json {

/**
 * Discriminates what kind of JSON value a Value currently holds.
 */
enum class Kind {
    Null,   ///< literal `null`
    Bool,   ///< literal `true` / `false`
    Number, ///< 64-bit signed integer (see Value::number)
    String, ///< string (see Value::string)
    Array,  ///< ordered list of values (see Value::array)
    Object  ///< string-keyed map (see Value::object)
};

/**
 * A single JSON value.
 *
 * Exactly one member is meaningful depending on `kind`:
 *   Null   -> (none)
 *   Bool   -> boolean
 *   Number -> number
 *   String -> string
 *   Array  -> array
 *   Object -> object (std::map keeps keys sorted, so stringify() output
 *            is deterministic — important for commit hashing).
 *
 * Objects are `std::map`, so duplicate keys parse with last-wins semantics
 * and serialization order is always ascending key order.
 */
struct Value {
    Kind kind = Kind::Null;
    bool boolean = false;
    int64_t number = 0;
    str string;
    Vec<Value> array;
    std::map<str, Value> object;

    /** Builds a Null value (also the default constructor). */
    static Value make_null() { return {}; }
    /** Builds a Bool value. */
    static Value make_bool(bool b) { return {.kind = Kind::Bool, .boolean = b}; }
    /** Builds a Number value. */
    static Value make_number(int64_t n) { return {.kind = Kind::Number, .number = n}; }
    /** Builds a String value. */
    static Value make_string(str s) { return {.kind = Kind::String, .string = std::move(s)}; }
    /** Builds an empty Array value. */
    static Value make_array() { return {.kind = Kind::Array}; }
    /** Builds an empty Object value. */
    static Value make_object() { return {.kind = Kind::Object}; }

    /** True if this value is an Object. */
    bool is_object() const { return kind == Kind::Object; }
    /** True if this value is an Array. */
    bool is_array() const { return kind == Kind::Array; }
    /** True if this value is a Bool. */
    bool is_bool() const { return kind == Kind::Bool; }
    /** True if this value is a String. */
    bool is_string() const { return kind == Kind::String; }
    /** True if this value is a Number. */
    bool is_number() const { return kind == Kind::Number; }

    /**
     * Object lookup by key.
     *
     * @param key key to find.
     * @return pointer to the value, or nullptr if this is not an Object or
     *         the key is absent. The pointer stays valid as long as the
     *         owning Value is not modified.
     */
    const Value* get(const str& key) const {
        if (!is_object()) return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
};

/**
 * Parses a JSON document.
 *
 * @param input the JSON text.
 * @return the parsed root value.
 * @throws std::runtime_error on any syntax error, trailing garbage, or an
 *         unsupported construct (floats/exponents). The error message
 *         includes the byte offset of the failure.
 */
Value parse(str_view input);

/**
 * Serializes a value to compact JSON (no whitespace).
 *
 * Object keys are emitted in ascending order (guaranteed by std::map).
 * Strings escape `"`, `\`, and control characters (< 0x20) as
 * `\uXXXX`; the ASCII subset round-trips losslessly.
 *
 * @param v the value to serialize.
 * @return compact JSON text.
 */
str stringify(const Value& v);

} // namespace uwu::json
