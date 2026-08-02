#pragma once

#include "common.hpp"
#include "regex.hpp"

namespace uwu {

/**
 * Matches working-directory paths against `.uwuignore` regex patterns.
 *
 * Semantics (per project decision):
 *   - Patterns are compiled as plain regexes (the shared uwu regex engine).
 *   - A FILE at relative path `p` is ignored if any pattern *occurs* in
 *     `p` (substring match, i.e. Regex::search). This makes both suffix
 *     patterns (`\.log$`) and bare-name patterns (`build`) work naturally.
 *   - A DIRECTORY at relative path `d` is ignored if any pattern occurs in
 *     `d`; the whole subtree under it is then skipped.
 *   - The repo directory `.uwu` is never subject to matching — callers
 *     (e.g. list_working_files) exclude it themselves.
 *
 * Empty lines and lines starting with `#` in `.uwuignore` are skipped.
 * `!` negation is NOT supported (v1 scope, per spec).
 */
class IgnoreMatcher {
public:
    /**
     * Compiles patterns from the repo's `.uwu/.uwuignore` file.
     *
     * @return a matcher with zero patterns if the file is absent/empty.
     * @throws std::runtime_error if any line fails to compile as a regex.
     */
    static IgnoreMatcher from_repo();

    /**
     * Compiles an explicit list of patterns (one regex per line).
     *
     * @param patterns raw pattern strings (comments/empties are filtered).
     * @throws std::runtime_error if any pattern fails to compile.
     */
    explicit IgnoreMatcher(const Vec<str>& patterns);

    /**
     * Tests whether a file at the given relative path is ignored.
     *
     * @param rel_path forward-slash relative path from repo root.
     * @return true if any pattern occurs in the path.
     */
    bool ignores(str_view rel_path) const;

    /**
     * Tests whether a directory at the given relative path is ignored.
     *
     * @param rel_dir forward-slash relative path of the directory.
     * @return true if any pattern occurs in the dir path.
     */
    bool ignores_dir(str_view rel_dir) const;

private:
    Vec<Regex> patterns_;
};

} // namespace uwu
