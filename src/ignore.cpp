#include "ignore.hpp"

#include <fstream>

namespace uwu {

namespace {

// Filters raw lines: drops empties and `#` comment lines, trims whitespace.
Vec<str> clean_patterns(const Vec<str>& raw) {
    Vec<str> out;
    out.reserve(raw.size());
    for (const auto& line : raw) {
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) start++;
        size_t end = line.size();
        while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
        if (start == end || line[start] == '#') continue;
        out.emplace_back(line.substr(start, end - start));
    }
    return out;
}

} // namespace

IgnoreMatcher IgnoreMatcher::from_repo() {
    Vec<str> lines;
    std::ifstream in(".uwu/.uwuignore");
    str line;
    while (std::getline(in, line)) lines.push_back(line);
    return IgnoreMatcher(lines);
}

IgnoreMatcher::IgnoreMatcher(const Vec<str>& patterns) {
    for (const auto& pat : clean_patterns(patterns))
        patterns_.emplace_back(pat); // throws on malformed regex
}

bool IgnoreMatcher::ignores(str_view rel_path) const {
    const str path(rel_path);
    for (const auto& re : patterns_)
        if (re.search(path)) return true;
    return false;
}

bool IgnoreMatcher::ignores_dir(str_view rel_dir) const {
    return ignores(rel_dir); // substring semantics cover dirs too
}

} // namespace uwu
