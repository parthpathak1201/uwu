#pragma once

#include <memory>
#include <string>

namespace uwu {

/**
 * A regular expression matcher built on a Thompson NFA.
 *
 * Supports: literals, `.` (any char except newline), `* + ?`,
 * `^ $` anchors, `|` alternation, `(...)` grouping, character
 * classes `[...]` with ranges and negation, and the escapes
 * `\d \w \s \D \W \S` (plus escaped literals).
 *
 * Not supported: `{m,n}` quantifiers, backreferences, lookahead.
 * Case-sensitive. No external dependencies.
 *
 * Constructing a Regex compiles the pattern into an NFA; construction
 * throws std::runtime_error on an invalid pattern.
 */
class Regex {
public:
    /**
     * Compiles a pattern into an NFA.
     *
     * @param pattern the regular expression source (e.g. `"\\d+"`).
     * @throws std::runtime_error on malformed patterns
     *         (e.g. unmatched paren, trailing backslash).
     */
    explicit Regex(const std::string& pattern);

    ~Regex();

    /** Move-only: a compiled regex is cheap to move, not copy. */
    Regex(Regex&&) noexcept;
    Regex& operator=(Regex&&) noexcept;

    Regex(const Regex&) = delete;
    Regex& operator=(const Regex&) = delete;

    /**
     * Tests whether the ENTIRE text matches the pattern.
     *
     * `^`/`$` anchors are honored; without them the whole input must
     * match (unlike search()).
     *
     * @param text input string
     * @return true if the full input matches, false otherwise.
     */
    bool match(const std::string& text) const;

    /**
     * Tests whether the pattern occurs ANYWHERE within the text.
     *
     * Searches for a substring match at any position. `^`/`$` anchors
     * constrain the start/end position when present.
     *
     * @param text input string
     * @return true if any substring matches, false otherwise.
     */
    bool search(const std::string& text) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
