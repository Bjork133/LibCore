#include "handlers/stats.hpp"
#include "handlers/logs.hpp"
#include "handlers/user.hpp"
#include "handlers/profile.hpp"
#include "cli/parser.hpp"
#include "handlers/help.hpp"
#include "handlers/settings.hpp"
#include "handlers/server.hpp"
#include "handlers/version.hpp"
#include "help/loader.hpp"
#include "util/paths.hpp"
#include "util/crypto.hpp"
#include <cstdio>
#include <exception>
#include <string>
#include <vector>
#include "core/settings.hpp"
#include "handlers/auth.hpp"
#include "handlers/book.hpp"

namespace {

void print_usage() {
    std::printf(
        "libcore — CLI-ядро для управления физической библиотекой.\n"
        "\n"
        "ИСПОЛЬЗОВАНИЕ:\n"
        "    libcore [ГЛОБАЛЬНЫЕ ФЛАГИ] <КОМАНДА> [АРГУМЕНТЫ] [ФЛАГИ]\n"
        "    libcore help [ЧТО-УГОДНО]\n"
        "\n"
        "ГЛОБАЛЬНЫЕ ФЛАГИ:\n"
        "    --json / --gui      Вывод в машиночитаемом JSON\n"
        "    -q, --quiet         Меньше вывода\n"
        "    -v, --verbose       Больше вывода\n"
        "    -V, --version       Показать версию\n"
        "    -h, --help          Показать справку\n"
        "\n"
        "За подробностями: libcore help <ЧТО-УГОДНО>\n"
    );
}

} // namespace

int main(int argc, char** argv) {
    using namespace libcore;

    try {
        paths::ensure_dirs();
        help::install_embedded_if_missing();
        core::ensure_settings_file();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "libcore: cannot initialize directories: %s\n", e.what());
    }

    auto inv = cli::parse(argc, argv);
    if (!inv.ok) {
        std::fprintf(stderr, "libcore: %s\n", inv.error.c_str());
        return 2;
    }

    if (inv.globals.version) {
        return handlers::cmd_version(inv.globals.json);
    }
    if (inv.globals.help && inv.command.empty()) {
        print_usage();
        return 0;
    }

    const std::string& cmd = inv.command;

    if (cmd.empty()) {
        print_usage();
        return 0;
    }

    try {
        if (cmd == "version") return handlers::cmd_version(inv.globals.json);
        if (cmd == "help")    return handlers::cmd_help(inv.args, inv.globals.json);
        if (cmd == "server")  return handlers::cmd_server(inv.args, inv.globals.json);
	if (cmd == "login")  return handlers::cmd_login (inv.args, inv.globals.json);
	if (cmd == "logout") return handlers::cmd_logout(inv.args, inv.globals.json);
        if (cmd == "book") return handlers::cmd_book(inv.args, inv.globals.json);
        if (cmd == "user")    return handlers::cmd_user(inv.args, inv.globals.json);
        if (cmd == "profile") return handlers::cmd_profile(inv.args, inv.globals.json);
	if (cmd == "whoami") return handlers::cmd_whoami(inv.args, inv.globals.json);
        if (cmd == "stats") return handlers::cmd_stats(inv.args, inv.globals.json);
        if (cmd == "logs")  return handlers::cmd_logs(inv.args, inv.globals.json);
        if (cmd == "settings") return handlers::cmd_settings(inv.args, inv.globals.json);
        std::fprintf(stderr, "libcore: unknown command '%s'\n", cmd.c_str());
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "libcore: %s\n", e.what());
        return 1;
    }
}
