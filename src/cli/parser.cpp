#include "cli/parser.hpp"

namespace libcore::cli {

namespace {

bool is_flag(std::string_view s) {
    return s.size() >= 2 && s[0] == '-' && s[1] != '-';
}

bool is_long_flag(std::string_view s) {
    return s.size() >= 3 && s[0] == '-' && s[1] == '-';
}

} // namespace

Invocation parse(int argc, char** argv) {
    Invocation inv;

    int i = 1;

    // Глобальные флаги — только до первой команды
    for (; i < argc; ++i) {
        std::string_view a = argv[i];

        if (a == "--") { ++i; break; }

        if (is_long_flag(a)) {
            if (a == "--json")        inv.globals.json = true;
            else if (a == "--gui")    inv.globals.json = true;   // --gui == --json
            else if (a == "--quiet")  inv.globals.quiet = true;
            else if (a == "--verbose")inv.globals.verbose = true;
            else if (a == "--version")inv.globals.version = true;
            else if (a == "--help")   inv.globals.help = true;
            else {
                inv.ok = false;
                inv.error = "unknown global flag: " + std::string(a);
                return inv;
            }
            continue;
        }

        if (is_flag(a)) {
            // короткие флаги, можно пачкой: -qv
            for (std::size_t k = 1; k < a.size(); ++k) {
                switch (a[k]) {
                    case 'q': inv.globals.quiet = true; break;
                    case 'v': inv.globals.verbose = true; break;
                    case 'V': inv.globals.version = true; break;
                    case 'h': inv.globals.help = true; break;
                    default:
                        inv.ok = false;
                        inv.error = std::string("unknown global flag: -") + a[k];
                        return inv;
                }
            }
            continue;
        }

        // Первый не-флаг — это команда
        break;
    }

    if (i < argc) {
        inv.command = argv[i++];
    }

    // Всё остальное — аргументы без интерпретации
    for (; i < argc; ++i) {
        inv.args.emplace_back(argv[i]);
    }

    return inv;
}

} // namespace libcore::cli
