#include "handlers/server.hpp"

#include "core/library_db.hpp"
#include "core/server_meta.hpp"
#include "db/database.hpp"
#include "db/migrations.hpp"
#include "net/http_server.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

namespace libcore::handlers {

using json = nlohmann::json;

namespace {

std::atomic<bool> g_stop_requested{false};
void signal_handler(int) { g_stop_requested.store(true); }

// --- парсинг ---

struct Options {
    std::vector<std::string> positional;
    std::optional<std::string> name;
    std::optional<std::string> host;
    std::optional<std::string> password;
    std::optional<int>         port;
    bool help = false;
};

Options parse_opts(const std::vector<std::string>& args) {
    Options o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "-h" || a == "--help") { o.help = true; continue; }

        auto take_value = [&](std::string_view flag) -> std::optional<std::string> {
            if (a.size() > flag.size() && a.starts_with(flag) && a[flag.size()] == '=') {
                return a.substr(flag.size() + 1);
            }
            if (a == flag) {
                if (i + 1 >= args.size())
                    throw std::runtime_error(std::string("flag ") +
                                             std::string(flag) + " requires a value");
                return args[++i];
            }
            return std::nullopt;
        };

        if (auto v = take_value("--name"))     { o.name = *v; continue; }
        if (auto v = take_value("--host"))     { o.host = *v; continue; }
        if (auto v = take_value("--password")) { o.password = *v; continue; }
        if (auto v = take_value("--port")) {
            try { o.port = std::stoi(*v); }
            catch (...) { throw std::runtime_error("invalid --port: " + *v); }
            continue;
        }

        if (!a.empty() && a[0] == '-') {
            throw std::runtime_error("unknown flag: " + a);
        }
        o.positional.push_back(a);
    }
    return o;
}

// --- ошибки ---

int fail(const std::string& msg, bool json_out) {
    if (json_out) {
        json j;
        j["ok"] = false;
        j["error"]["code"] = "server_error";
        j["error"]["message"] = msg;
        std::fprintf(stderr, "%s\n", j.dump().c_str());
    } else {
        std::fprintf(stderr, "libcore: %s\n", msg.c_str());
    }
    return 1;
}

// --- ввод пароля ---

std::string prompt_password(const char* prompt) {
    std::fprintf(stderr, "%s", prompt);
    std::fflush(stderr);

    std::string s;
#ifndef _WIN32
    termios oldt{};
    tcgetattr(STDIN_FILENO, &oldt);
    termios newt = oldt;
    newt.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == 8) {
            if (!s.empty()) s.pop_back();
            continue;
        }
        s.push_back(c);
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::fprintf(stderr, "\n");
#else
    std::getline(std::cin, s);
#endif
    return s;
}

// --- выбор библиотеки ---

std::string pick_library(const Options& o) {
    if (!o.positional.empty()) return o.positional[0];
    auto libs = core::list_libraries();
    if (libs.size() == 1) return libs[0];
    if (libs.empty()) throw std::runtime_error("no libraries found");
    throw std::runtime_error("multiple libraries — specify a name");
}

// --- server init ---

int do_init(const Options& o, bool json_out) {
    std::string name = o.name.value_or("");
    std::string host = o.host.value_or("");
    int port = o.port.value_or(8420);

    if (name.empty()) {
        char buf[256] = {};
        if (gethostname(buf, sizeof(buf) - 1) == 0 && buf[0]) {
            name = buf;
        } else {
            name = "library";
        }
    }
    if (host.empty()) {
        const char* u = std::getenv("USER");
        if (!u) u = std::getenv("USERNAME");
        host = u ? u : "host";
    }

    std::string password;
    if (o.password) {
        password = *o.password;
    } else if (json_out) {
        return fail("--password is required in --json mode", json_out);
    } else {
        password = prompt_password("Пароль для хоста: ");
        std::string confirm = prompt_password("Повторите пароль: ");
        if (password != confirm) {
            return fail("passwords do not match", json_out);
        }
    }

    if (password.empty()) {
        return fail("password cannot be empty", json_out);
    }

    try {
        auto meta = core::create_library(name, host, password, port);
        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"] = {
                {"name",       meta.name},
                {"host",       meta.host},
                {"port",       meta.port},
                {"code",       meta.code},
                {"created_at", meta.created_at},
            };
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("Библиотека создана.\n");
            std::printf("  имя:  %s\n", meta.name.c_str());
            std::printf("  хост: %s\n", meta.host.c_str());
            std::printf("  порт: %d\n", meta.port);
            std::printf("  код:  %s\n", meta.code.c_str());
            std::printf("\nПуть: %s\n", core::library_dir(meta.name).string().c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        return fail(e.what(), json_out);
    }
}

// --- server list ---

int do_list(bool json_out) {
    auto libs = core::list_libraries();
    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["libraries"] = libs;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (libs.empty()) {
            std::printf("Библиотек не найдено.\n");
        } else {
            std::printf("Библиотеки (%zu):\n", libs.size());
            for (const auto& n : libs) {
                std::printf("  %s\n", n.c_str());
            }
        }
    }
    return 0;
}

// --- server code ---

int do_code(const Options& o, bool json_out) {
    std::string name;
    try { name = pick_library(o); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    auto meta = core::read_meta(name);
    if (!meta) return fail("cannot read server.json for " + name, json_out);

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["name"] = meta->name;
        j["data"]["code"] = meta->code;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("%s\n", meta->code.c_str());
    }
    return 0;
}

// --- server status ---

int do_status(const Options& o, bool json_out) {
    std::string name;
    try { name = pick_library(o); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    auto meta = core::read_meta(name);
    if (!meta) return fail("cannot read server.json for " + name, json_out);

    long long n_users = 0, n_books = 0, n_loans = 0, n_requests = 0;
    try {
        auto database = core::open_library_db(name);
        auto q = [&](const char* sql) -> long long {
            try {
                auto r = database.query(sql);
                if (r.empty() || r[0].empty()) return 0;
                return db::row_int(r[0], 0);
            } catch (...) { return 0; }
        };
        n_users    = q("SELECT COUNT(*) FROM users;");
        n_books    = q("SELECT COUNT(*) FROM books;");
        n_loans    = q("SELECT COUNT(*) FROM loans;");
        n_requests = q("SELECT COUNT(*) FROM requests;");
    } catch (...) {}

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"] = {
            {"name",       meta->name},
            {"host",       meta->host},
            {"port",       meta->port},
            {"code",       meta->code},
            {"created_at", meta->created_at},
            {"users",      n_users},
            {"books",      n_books},
            {"loans",      n_loans},
            {"requests",   n_requests},
        };
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Библиотека: %s\n", meta->name.c_str());
        std::printf("  хост:     %s\n", meta->host.c_str());
        std::printf("  порт:     %d\n", meta->port);
        std::printf("  код:      %s\n", meta->code.c_str());
        std::printf("  юзеров:   %lld\n", n_users);
        std::printf("  книг:     %lld\n", n_books);
        std::printf("  выдач:    %lld\n", n_loans);
        std::printf("  запросов: %lld\n", n_requests);
    }
    return 0;
}

// --- server start ---

int do_start(const Options& o, bool json_out) {
    std::string name;
    try { name = pick_library(o); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    auto meta = core::read_meta(name);
    if (!meta) return fail("cannot read server.json for " + name, json_out);

    int port = o.port.value_or(meta->port);

    // Проверим, что порт свободен (быстрый bind-тест)
    {
        net::HttpServer probe(name, port);
        // HttpServer::start() сам бросит, но мы хотим проверить ДО запуска
        // Поэтому просто создаём и сразу удаляем. Реальный bind — в start().
        (void)probe;
    }

    net::HttpServer server(name, port);

    // Сигналы
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    g_stop_requested.store(false);

    std::fprintf(stderr, "libcore: starting server '%s' on port %d\n",
                 name.c_str(), port);
    std::fprintf(stderr, "libcore: press Ctrl+C to stop\n");

    std::thread server_thread([&]() {
        try {
            server.start();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "libcore: server error: %s\n", e.what());
            g_stop_requested.store(true);
        }
    });

    // Ждём сигнала
    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::fprintf(stderr, "libcore: stopping server...\n");
    server.stop();
    if (server_thread.joinable()) server_thread.join();

    std::fprintf(stderr, "libcore: server stopped\n");
    return 0;
}

} // namespace

int cmd_server(const std::vector<std::string>& args, bool json_out) {
    if (args.empty()) {
        return fail("usage: libcore server <init|list|code|status|start>", json_out);
    }

    std::string sub = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    Options o;
    try {
        o = parse_opts(rest);
    } catch (const std::exception& e) {
        return fail(e.what(), json_out);
    }

    if (o.help) {
        std::printf("libcore server %s — см. `libcore help server %s`\n",
                    sub.c_str(), sub.c_str());
        return 0;
    }

    if (sub == "init")   return do_init(o, json_out);
    if (sub == "list")   return do_list(json_out);
    if (sub == "code")   return do_code(o, json_out);
    if (sub == "status") return do_status(o, json_out);
    if (sub == "start")  return do_start(o, json_out);

    return fail("unknown subcommand: server " + sub, json_out);
}

} // namespace libcore::handlers
