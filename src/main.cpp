#include "commands.hpp"
#include "common.hpp"

#include <algorithm>
#include <iostream>

static str_view trim(str_view sv) {
    constexpr auto drop = " \t\n\r\f\v";
    sv.remove_prefix(std::min(sv.find_first_not_of(drop), sv.size()));
    sv.remove_suffix(sv.size() - std::min(sv.find_last_not_of(drop) + 1, sv.size()));
    return sv;
}

int main(const int argc, char *argv[]) {
    Vec<str> tokens;
    for (int i = 1; i < argc; ++i) {
        if (str_view clean_token = trim(argv[i]); !clean_token.empty()) {
            tokens.emplace_back(clean_token);
        }
    }

    //map out all the commands (I have this intuition that this might be an overkill for this kinda project)
    init_cmd();


    const auto cmd = cmd_map.find(resolve_type(tokens));
    if (cmd == cmd_map.end()) {
        std::cerr << "error: unknown command\n";
        return 1;
    }
    cmd->second(tokens);
    return 0;
}
