#include "json.hpp"

#include <cctype>
#include <charconv>

namespace uwu::json {

namespace {

// ---------------------------------------------------------------------------
// Parser: hand-rolled recursive descent over a str_view cursor.
// ---------------------------------------------------------------------------

class Parser {
public:
    explicit Parser(str_view input) : s_(input) {}

    Value parse_value() {
        skip_ws();
        if (done())
            fail("unexpected end of input");
        switch (s_[pos_]) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': return parse_string();
            case 't': return parse_literal("true", Value::make_bool(true));
            case 'f': return parse_literal("false", Value::make_bool(false));
            case 'n': return parse_literal("null", Value::make_null());
            default:
                if (s_[pos_] == '-' || std::isdigit(static_cast<unsigned char>(s_[pos_])))
                    return parse_number();
                fail("unexpected character");
        }
        return {};
    }

private:
    str_view s_;
    size_t pos_ = 0;

public:
    [[nodiscard]] bool done() const { return pos_ >= s_.size(); }

    void skip_ws() {
        while (!done() && std::isspace(static_cast<unsigned char>(s_[pos_])))
            pos_++;
    }

private:

    [[noreturn]] void fail(const str& msg) const {
        throw std::runtime_error("json: " + msg + " at offset " + std::to_string(pos_));
    }

    char peek() const { return s_[pos_]; }
    void expect(char c) {
        if (done() || s_[pos_] != c)
            fail(std::string("expected '") + c + "'");
        pos_++;
    }

    Value parse_literal(const char* word, Value v) {
        for (size_t i = 0; word[i]; i++) {
            if (done() || s_[pos_] != word[i])
                fail("invalid literal");
            pos_++;
        }
        return v;
    }

    Value parse_object() {
        Value out = Value::make_object();
        expect('{');
        skip_ws();
        if (!done() && peek() == '}') { pos_++; return out; }
        while (true) {
            skip_ws();
            if (done() || peek() != '"')
                fail("expected string key");
            Value key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            Value val = parse_value();
            out.object[key.string] = std::move(val); // last duplicate wins
            skip_ws();
            if (!done() && peek() == ',') { pos_++; continue; }
            expect('}');
            return out;
        }
    }

    Value parse_array() {
        Value out = Value::make_array();
        expect('[');
        skip_ws();
        if (!done() && peek() == ']') { pos_++; return out; }
        while (true) {
            skip_ws();
            out.array.push_back(parse_value());
            skip_ws();
            if (!done() && peek() == ',') { pos_++; continue; }
            expect(']');
            return out;
        }
    }

    // Parses a JSON string (with escapes) into a UTF-8-ish std::string.
    // \uXXXX yields the code point as UTF-8; surrogate pairs are combined.
    Value parse_string() {
        expect('"');
        str out;
        while (true) {
            if (done())
                fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') break;
            if (c == '\\') {
                if (done()) fail("unterminated escape");
                const char e = s_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': out.append(parse_unicode_escape()); break;
                    default: fail("invalid escape");
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail("raw control character in string");
            } else {
                out.push_back(c);
            }
        }
        return Value::make_string(std::move(out));
    }

    // Parses \uXXXX, optionally followed by a low surrogate; emits UTF-8.
    str parse_unicode_escape() {
        const uint32_t hi = read_hex4();
        if (hi >= 0xD800 && hi <= 0xDBFF) {
            // High surrogate: expect \uDC00-\uDFFF and combine.
            if (s_.size() - pos_ >= 6 && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                pos_ += 2;
                const uint32_t lo = read_hex4();
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    return utf8_encode(0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00));
                }
                fail("invalid low surrogate");
            }
            fail("dangling high surrogate");
        }
        if (hi >= 0xDC00 && hi <= 0xDFFF)
            fail("unexpected low surrogate");
        return utf8_encode(hi);
    }

    uint32_t read_hex4() {
        if (s_.size() - pos_ < 4)
            fail("truncated \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else fail("invalid hex digit");
        }
        return v;
    }

    static str utf8_encode(uint32_t cp) {
        str out;
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }

    // Integers only (int64_t), matching the data we store. Floats/exponents
    // are rejected loudly rather than silently truncated.
    Value parse_number() {
        const size_t start = pos_;
        if (peek() == '-') pos_++;
        if (done() || !std::isdigit(static_cast<unsigned char>(peek())))
            fail("invalid number");
        if (peek() == '0') pos_++;
        else while (!done() && std::isdigit(static_cast<unsigned char>(peek()))) pos_++;
        if (!done() && (peek() == '.' || peek() == 'e' || peek() == 'E'))
            fail("floats/exponents not supported");
        int64_t n = 0;
        const auto res = std::from_chars(s_.data() + start, s_.data() + pos_, n);
        if (res.ec != std::errc())
            fail("number out of range");
        return Value::make_number(n);
    }
};

// ---------------------------------------------------------------------------
// Writer: compact serialization with sorted object keys.
// ---------------------------------------------------------------------------

void write_string(const str& s, str& out);

void write_value(const Value& v, str& out) {
    switch (v.kind) {
    case Kind::Null:
        out += "null";
        break;
    case Kind::Bool:
        out += v.boolean ? "true" : "false";
        break;
    case Kind::Number:
        out += std::to_string(v.number);
        break;
    case Kind::String:
        write_string(v.string, out);
        break;
    case Kind::Array:
        out.push_back('[');
        for (size_t i = 0; i < v.array.size(); i++) {
            if (i) out.push_back(',');
            write_value(v.array[i], out);
        }
        out.push_back(']');
        break;
    case Kind::Object:
        out.push_back('{');
        size_t i = 0;
        for (const auto& [key, val] : v.object) { // std::map = ascending order
            if (i++) out.push_back(',');
            write_string(key, out);
            out.push_back(':');
            write_value(val, out);
        }
        out.push_back('}');
        break;
    }
}

void write_string(const str& s, str& out) {
    out.push_back('"');
    static constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[c >> 4]);
                    out.push_back(hex[c & 0xF]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Value parse(str_view input) {
    Parser p(input);
    Value root = p.parse_value();
    p.skip_ws(); // trailing whitespace is legal JSON (RFC 8259)
    if (!p.done())
        throw std::runtime_error("json: trailing characters after root value");
    return root;
}

str stringify(const Value& v) {
    str out;
    write_value(v, out);
    return out;
}

} // namespace uwu::json
