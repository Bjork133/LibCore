#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libcore::cli {

struct GlobalFlags {
    bool json = false;
    bool quiet = false;
    bool verbose = false;
    bool version = false;
    bool help = false;
};

struct Invocation {
    GlobalFlags globals;
    std::string command;                 // "book", "help", "version", ...
    std::vector<std::string> args;       // всё после команды, как есть
    bool ok = true;                      // false при явной ошибке парсинга
    std::string error;                   // текст ошибки, если ok=false
};

// Разбирает argv[1..]. Только глобальные флаги до первой команды.
// Всё остальное складывает в args без интерпретации — этим занимаются handlers.
Invocation parse(int argc, char** argv);

} // namespace libcore::cli
