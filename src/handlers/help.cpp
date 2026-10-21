// help.cpp
#include "handlers/help.hpp"

#include "help/loader.hpp"

#include <cstdio>
#include <string>

namespace libcore::handlers {

namespace {

// Склеивает аргументы без пробелов, в нижний регистр.
std::string glue(const std::vector<std::string>& args) {
    std::string out;
    for (const auto& a : args) {
        for (char c : a) {
            char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lc == '-') lc = '_';
            out.push_back(lc);
        }
    }
    return out;
}

} // namespace

int cmd_help(const std::vector<std::string>& args, bool json) {
    std::string name = args.empty() ? std::string("help") : glue(args);

    auto content = help::find(name);
    if (!content) {
        if (json) {
            std::fprintf(stderr,
                "{\"ok\":false,\"error\":{\"code\":\"help_not_found\","
                "\"message\":\"no help file for '%s' (looked at %s)\"}}\n",
                name.c_str(), help::search_path_for(name).c_str());
        } else {
            std::fprintf(stderr, "help: no help file for '%s'\n", name.c_str());
        }
        return 1;
    }

    if (json) {
        // JSON-экранирование минимальное — только кавычки, бэкслеши, \n, \r, \t
        std::string esc;
        esc.reserve(content->size() + 16);
        for (char c : *content) {
            switch (c) {
                case '"':  esc += "\\\""; break;
                case '\\': esc += "\\\\"; break;
                case '\n': esc += "\\n";  break;
                case '\r': esc += "\\r";  break;
                case '\t': esc += "\\t";  break;
                default:   esc += c;
            }
        }
        std::printf("{\"ok\":true,\"data\":{\"text\":\"%s\"}}\n", esc.c_str());
    } else {
        std::fwrite(content->data(), 1, content->size(), stdout);
        if (content->empty() || content->back() != '\n') std::putchar('\n');
    }
    return 0;
}

} // namespace libcore::handlers
