#include "regex.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace uwu {

namespace {
    enum TokenType : uint8_t {
        T_CHAR, T_DOT, T_STAR, T_PLUS, T_QMARK,
        T_CARET, T_DOLLAR,
        T_LPAREN, T_RPAREN, T_LBRACK, T_RBRACK, T_PIPE,
        T_ESC_D, T_ESC_W, T_ESC_S,
        T_ESC_D_CAP, T_ESC_W_CAP, T_ESC_S_CAP,
        T_END
    };
}

namespace {
    struct Token {
        TokenType type;
        char c;
    };
}

namespace {
    class TokenStream {
        std::vector<Token> tokens_;
        size_t pos_ = 0;
    public:
        explicit TokenStream(const std::string& pattern) {
            for (size_t i = 0; i < pattern.size(); i++) {
                switch (char ch = pattern[i]) {
                    case '.': push(T_DOT); break;
                    case '*': push(T_STAR); break;
                    case '+': push(T_PLUS); break;
                    case '?': push(T_QMARK); break;
                    case '^': push(T_CARET); break;
                    case '$': push(T_DOLLAR); break;
                    case '(': push(T_LPAREN); break;
                    case ')': push(T_RPAREN); break;
                    case '[': push(T_LBRACK); break;
                    case ']': push(T_RBRACK); break;
                    case '|': push(T_PIPE); break;
                    case '\\':
                        if (i + 1 >= pattern.size())
                            throw std::runtime_error("trailing backslash");
                        i++;
                        ch = pattern[i];
                        switch (ch) {
                            case 'd': push(T_ESC_D); break;
                            case 'D': push(T_ESC_D_CAP); break;
                            case 'w': push(T_ESC_W); break;
                            case 'W': push(T_ESC_W_CAP); break;
                            case 's': push(T_ESC_S); break;
                            case 'S': push(T_ESC_S_CAP); break;
                            default: push_char(ch); break;
                        }
                        break;
                    default:
                        push_char(ch);
                }
            }
            push(T_END);
        }

        [[nodiscard]] Token peek() const { return tokens_[pos_]; }
        Token advance() { return tokens_[pos_++]; }
        [[nodiscard]] bool eof() const { return tokens_[pos_].type == T_END; }

    private:
        void push(TokenType t) { tokens_.push_back({t, 0}); }
        void push_char(char c) { tokens_.push_back({T_CHAR, c}); }
    };
}

namespace {
    // NFA state: up to 2 outgoing arrows. Types:
    //   -1 = accept
    //    0 = split (epsilon, follows next[0] and/or next[1])
    //  1-255 = match literal character via next[0]
    //  256 = any character except newline via next[0]
    //  257 = char class via next[0] (class bits stored as 4 uint64_t)
    struct State {
        int type;
        int next[2];
        uint64_t class_bits[4];
    };
}

namespace {
    struct PatchSlot {
        int state;
        int slot;
    };
}

static PatchSlot ps(int s, int sl) { return {s, sl}; }

using PatchList = std::vector<PatchSlot>;

namespace {
    struct Frag {
        int start;
        PatchList out;
    };
}

static void set_bit(uint64_t* bits, uint8_t c) {
    bits[c / 64] |= (1ULL << (c % 64));
}

static bool test_bit(const uint64_t* bits, uint8_t c) {
    return (bits[c / 64] >> (c % 64)) & 1;
}

// Forward declarations
static Frag parse_alternation(TokenStream& ts, std::vector<State>& states);

static int alloc_state(std::vector<State>& states) {
    const int id = static_cast<int>(states.size());
    states.push_back({.type = 0, .next = {-1, -1}, .class_bits = {0, 0, 0, 0}});
    return id;
}

static void patch(const PatchList& list, const int target, std::vector<State>& states) {
    for (auto [s, slot] : list) {
        states[s].next[slot] = target;
    }
}

static Frag make_char(char c, std::vector<State>& states) {
    int s = alloc_state(states);
    int e = alloc_state(states);
    states[s] = {.type = static_cast<uint8_t>(c), .next = {e, -1}, .class_bits = {0, 0, 0, 0}};
    return {.start = s, .out = {ps(e, 0)}};
}

static Frag make_dot(std::vector<State>& states) {
    int s = alloc_state(states);
    int e = alloc_state(states);
    states[s] = {256, {e, -1}, {0, 0, 0, 0}};
    return {.start = s, .out = {ps(e, 0)}};
}

static Frag make_class(const uint64_t* bits, std::vector<State>& states) {
    int s = alloc_state(states);
    int e = alloc_state(states);
    states[s] = {.type = 257, .next = {e, -1}, .class_bits = {bits[0], bits[1], bits[2], bits[3]}};
    return {.start = s, .out = {ps(e, 0)}};
}

static Frag parse_class(TokenStream& ts, std::vector<State>& states) {
    uint64_t bits[4] = {0, 0, 0, 0};
    bool negate = false;

    Token t = ts.peek();
    if (t.type == T_CARET) {
        ts.advance();
        negate = true;
    }

    bool first = true;
    uint8_t range_start = 0;
    while (true) {
        t = ts.peek();
        if (t.type == T_RBRACK || t.type == T_END) break;

        if (t.type == T_CHAR && t.c == ']' && !first) {
            break;
        }
        ts.advance();

        if (t.type == T_CHAR && t.c == '-' && !first && ts.peek().type == T_CHAR) {
            const uint8_t range_end = static_cast<uint8_t>(ts.peek().c);
            ts.advance();
            for (uint8_t c = range_start; c <= range_end; c++)
                set_bit(bits, c);
            continue;
        }

        switch (t.type) {
        case T_CHAR:
            range_start = static_cast<uint8_t>(t.c);
            set_bit(bits, range_start);
            break;
        case T_ESC_D:
            for (uint8_t c = '0'; c <= '9'; c++) set_bit(bits, c);
            break;
        case T_ESC_W:
            for (uint8_t c = 0; c < 255; c++)
                if (isalnum(c) || c == '_') set_bit(bits, c);
            break;
        case T_ESC_S:
            for (uint8_t c = 0; c < 255; c++)
                if (isspace(c)) set_bit(bits, c);
            break;
        default:
            range_start = static_cast<uint8_t>(t.c);
            set_bit(bits, range_start);
        }
        first = false;
    }

    if (ts.peek().type == T_RBRACK) ts.advance();

    if (negate) {
        uint64_t neg_bits[4] = {0, 0, 0, 0};
        for (int c = 0; c < 256; c++) {
            if (!test_bit(bits, static_cast<uint8_t>(c)) && static_cast<uint8_t>(c) != '\n')
                set_bit(neg_bits, static_cast<uint8_t>(c));
        }
        return make_class(neg_bits, states);
    }

    return make_class(bits, states);
}

static Frag parse_atom(TokenStream& ts, std::vector<State>& states) {
    Token t = ts.advance();
    switch (t.type) {
    case T_CHAR:
        return make_char(t.c, states);
    case T_DOT:
        return make_dot(states);
    case T_LPAREN: {
        Frag inner = parse_alternation(ts, states);
        t = ts.advance();
        if (t.type != T_RPAREN)
            throw std::runtime_error("unmatched (");
        return inner;
    }
    case T_LBRACK:
        return parse_class(ts, states);
    case T_CARET:
        return make_char('^', states);
    case T_DOLLAR:
        return make_char('$', states);
    case T_ESC_D:
    case T_ESC_W:
    case T_ESC_S:
    case T_ESC_D_CAP:
    case T_ESC_W_CAP:
    case T_ESC_S_CAP: {
        uint64_t bits[4] = {0, 0, 0, 0};
        bool neg = false;
        switch (t.type) {
        case T_ESC_D:
            for (uint8_t c = '0'; c <= '9'; c++) set_bit(bits, c);
            break;
        case T_ESC_D_CAP: neg = true;
            for (uint8_t c = '0'; c <= '9'; c++) set_bit(bits, c);
            break;
        case T_ESC_W:
            for (int c = 0; c < 256; c++)
                if (isalnum(c) || c == '_') set_bit(bits, static_cast<uint8_t>(c));
            break;
        case T_ESC_W_CAP: neg = true;
            for (int c = 0; c < 256; c++)
                if (isalnum(c) || c == '_') set_bit(bits, static_cast<uint8_t>(c));
            break;
        case T_ESC_S:
            for (int c = 0; c < 256; c++)
                if (isspace(c)) set_bit(bits, static_cast<uint8_t>(c));
            break;
        case T_ESC_S_CAP: neg = true;
            for (int c = 0; c < 256; c++)
                if (isspace(c)) set_bit(bits, static_cast<uint8_t>(c));
            break;
        default: break;
        }
        if (neg) {
            uint64_t neg_bits[4] = {0, 0, 0, 0};
            for (int c = 0; c < 256; c++)
                if (!test_bit(bits, static_cast<uint8_t>(c)))
                    set_bit(neg_bits, static_cast<uint8_t>(c));
            return make_class(neg_bits, states);
        }
        return make_class(bits, states);
    }
    default:
        throw std::runtime_error("unexpected token");
    }
}

static Frag parse_repeat(TokenStream& ts, std::vector<State>& states) {
    Frag atom = parse_atom(ts, states);
    auto [type, c] = ts.peek();
    if (type == T_STAR) {
        ts.advance();
        int s = alloc_state(states);
        int e = alloc_state(states);
        states[s] = {0, {atom.start, e}, {0, 0, 0, 0}};
        patch(atom.out, s, states);
        return {s, {ps(e, 0)}};
    }
    if (type == T_PLUS) {
        ts.advance();
        int s = alloc_state(states);
        int e = alloc_state(states);
        patch(atom.out, s, states);
        states[s] = {0, {atom.start, e}, {0, 0, 0, 0}};
        return {atom.start, {ps(e, 0)}};
    }
    if (type == T_QMARK) {
        ts.advance();
        int s = alloc_state(states);
        int e = alloc_state(states);
        states[s] = {0, {atom.start, e}, {0, 0, 0, 0}};
        patch(atom.out, e, states);
        return {s, {ps(e, 0)}};
    }
    return atom;
}

static Frag parse_sequence(TokenStream& ts, std::vector<State>& states) {
    Frag result = {.start = -1, .out = {}};
    while (true) {
        auto [type, c] = ts.peek();
        if (type == T_PIPE || type == T_RPAREN ||
            type == T_RBRACK || type == T_END)
            break;
        Frag next = parse_repeat(ts, states);
        if (result.start == -1) {
            result = next;
        } else {
            patch(result.out, next.start, states);
            result.out = std::move(next.out);
        }
    }
    if (result.start == -1) {
        int s = alloc_state(states);
        result = {s, {ps(s, 0)}};
    }
    return result;
}

static Frag parse_alternation(TokenStream& ts, std::vector<State>& states) {
    Frag left = parse_sequence(ts, states);
    while (ts.peek().type == T_PIPE) {
        ts.advance();
        auto [start, out] = parse_sequence(ts, states);
        const int s = alloc_state(states);
        states[s] = {0, {left.start, start}, {0, 0, 0, 0}};
        const int e = alloc_state(states);
        patch(left.out, e, states);
        patch(out, e, states);
        left = {.start = s, .out = {ps(e, 0)}};
    }
    return left;
}

namespace {
    // --- NFA compiled state ---
    struct CompiledNFA {
        std::vector<State> states;
        int start{};
        bool anchored_start;
        bool anchored_end;

        explicit CompiledNFA(const std::string& pattern) {
            std::string pat = pattern;
            anchored_start = !pat.empty() && pat[0] == '^';
            anchored_end = !pat.empty() && pat.back() == '$';
            if (anchored_start) pat = pat.substr(1);
            if (anchored_end) pat.pop_back();

            TokenStream ts(pat);
            auto [start, out] = parse_alternation(ts, states);

            auto [type, c] = ts.peek();
            if (type != T_END) {
                if (type == T_RPAREN)
                    throw std::runtime_error("unmatched )");
                throw std::runtime_error("unexpected token");
            }

            const int accept = alloc_state(states);
            states[accept].type = -1;
            patch(out, accept, states);
            this->start = start;
        }
    };
}

// --- Simulation ---
struct Regex::Impl {
    CompiledNFA nfa;

    explicit Impl(const std::string& pattern) : nfa(pattern) {}

    [[nodiscard]] std::vector<int> epsilon_closure(const std::vector<int>& ids) const {
        std::vector<bool> visited(nfa.states.size(), false);
        std::vector<int> stack = ids;
        std::vector<int> result;
        while (!stack.empty()) {
            int id = stack.back(); stack.pop_back();
            if (id < 0 || id >= static_cast<int>(nfa.states.size()) || visited[id])
                continue;
            visited[id] = true;
            result.push_back(id);
            if (const auto& st = nfa.states[id]; st.type == 0) {
                for (int i : st.next)
                    if (i >= 0) stack.push_back(i);
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<int> move_one(const std::vector<int>& ids, const uint8_t ch) const {
        std::vector<int> result;
        for (const int id : ids) {
            if (id < 0 || id >= static_cast<int>(nfa.states.size())) continue;
            const auto&[type, next, class_bits] = nfa.states[id];
            int target = -1;
            if (type >= 1 && type <= 255) {
                if (static_cast<uint8_t>(type) == ch) target = next[0];
            } else if (type == 256) {
                if (ch != '\n') target = next[0];
            } else if (type == 257) {
                if (test_bit(class_bits, ch)) target = next[0];
            }
            if (target >= 0) result.push_back(target);
        }
        return result;
    }

    [[nodiscard]] bool has_accept(const std::vector<int>& ids) const {
        for (const int id : ids)
            if (id >= 0 && id < static_cast<int>(nfa.states.size()) &&
                nfa.states[id].type == -1)
                return true;
        return false;
    }
};

Regex::Regex(const std::string& pattern)
    : impl_(std::make_unique<Impl>(pattern)) {}
Regex::~Regex() = default;
Regex::Regex(Regex&&) noexcept = default;
Regex& Regex::operator=(Regex&&) noexcept = default;

bool Regex::match(const std::string& text) const {
    auto cur = impl_->epsilon_closure({impl_->nfa.start});
    for (char p : text) {
        cur = impl_->epsilon_closure(
            impl_->move_one(cur, static_cast<uint8_t>(p)));
    }
    return impl_->has_accept(cur);
}

bool Regex::search(const std::string& text) const {
    auto try_start = [&](size_t start) -> bool {
        auto cur = impl_->epsilon_closure({impl_->nfa.start});
        for (size_t p = start; p < text.size(); p++) {
            cur = impl_->epsilon_closure(
                impl_->move_one(cur, static_cast<uint8_t>(text[p])));
        }
        return impl_->has_accept(cur);
    };

    if (impl_->nfa.anchored_end) {
        if (impl_->nfa.anchored_start) {
            return try_start(0);
        }
        for (size_t s = 0; s <= text.size(); s++) {
            if (try_start(s)) return true;
        }
        return false;
    }

    // Not end-anchored: can end anywhere
    if (impl_->nfa.anchored_start) {
        auto cur = impl_->epsilon_closure({impl_->nfa.start});
        if (impl_->has_accept(cur)) return true;
        for (char p : text) {
            cur = impl_->epsilon_closure(
                impl_->move_one(cur, static_cast<uint8_t>(p)));
            if (impl_->has_accept(cur)) return true;
        }
        return false;
    }

    auto cur = impl_->epsilon_closure({impl_->nfa.start});
    if (impl_->has_accept(cur)) return true;
    for (char p : text) {
        cur = impl_->epsilon_closure(
            impl_->move_one(cur, (uint8_t)p));
        if (impl_->has_accept(cur)) return true;
        auto fresh = impl_->epsilon_closure({impl_->nfa.start});
        cur.insert(cur.end(), fresh.begin(), fresh.end());
    }
    return false;
}

}
