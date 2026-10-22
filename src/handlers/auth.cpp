#include "core/server_meta.hpp"
#include "handlers/auth.hpp"

#include "api/auth.hpp"
#include "api/common.hpp"
#include "core/profiles.hpp"
#include "net/http_client.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#  include <termios.h>
#  include <unistd.h>
#endif

namespace libcore::handlers {

using json = nlohmann::json;

namespace {

struct AuthOpts {
    std::vector<std::string> positional;
    std::optional<std::string> profile;
    std::optional<std::string> library;
    std::optional<std::string> user;
    std::optional<std::string> password;
    bool remote = false;
    std::optional<std::string> host;
    std::optional<int>         port;
    std::optional<std::string> code;
};

AuthOpts parse_opts(const std::vector<std::string>& args) {
    AuthOpts o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];

        auto take = [&](std::string_view flag) -> std::optional<std::string> {
            if (a.size() > flag.size() && a.starts_with(flag) && a[flag.size()] == '=') {
                return a.substr(flag.size() + 1);
            }
            if (a == flag) {
                if (i + 1 >= args.size())
                    throw std::runtime_error(std::string("flag ") + std::string(flag) + " requires a value");
                return args[++i];
            }
            return std::nullopt;
        };

        if (auto v = take("--profile"))  { o.profile  = *v; continue; }
        if (auto v = take("--library"))  { o.library  = *v; continue; }
        if (auto v = take("--user"))     { o.user     = *v; continue; }
        if (auto v = take("--password")) { o.password = *v; continue; }
        if (auto v = take("--host"))     { o.host     = *v; continue; }
        if (auto v = take("--code"))     { o.code     = *v; continue; }
        if (auto v = take("--port")) {
            try { o.port = std::stoi(*v); }
            catch (...) { throw std::runtime_error("invalid --port: " + *v); }
            continue;
        }
        if (a == "--remote") { o.remote = true; continue; }

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
        o.positional.push_back(a);
    }
    return o;
}

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

std::string prompt_line(const char* prompt) {
    std::fprintf(stderr, "%s", prompt);
    std::fflush(stderr);
    std::string s;
    std::getline(std::cin, s);
    return s;
}

int fail(const std::string& msg, bool json_out, const char* code = "auth_error") {
    if (json_out) {
        json j;
        j["ok"] = false;
        j["error"]["code"] = code;
        j["error"]["message"] = msg;
        std::fprintf(stderr, "%s\n", j.dump().c_str());
    } else {
        std::fprintf(stderr, "libcore: %s\n", msg.c_str());
    }
    return 1;
}

std::string pick_library(const std::optional<std::string>& explicit_name) {
    if (explicit_name && !explicit_name->empty()) return *explicit_name;
    auto libs = core::list_libraries();
    if (libs.empty()) throw std::runtime_error("no libraries found on this machine");
    if (libs.size() == 1) return libs[0];
    throw std::runtime_error("multiple libraries — specify --library");
}

// --- общий вывод login ---

int print_login_result(const json& resp, const std::string& profile_name, bool json_out) {
    if (!resp.value("ok", false)) {
        std::string code = resp["error"].value("code", "auth_error");
        std::string msg  = resp["error"].value("message", "");
        if (json_out) {
            std::fprintf(stderr, "%s\n", resp.dump().c_str());
        } else {
            std::fprintf(stderr, "libcore: %s\n", msg.c_str());
        }
        return 1;
    }

    if (json_out) {
        json j = resp;
        j["data"]["profile"] = profile_name;
        std::printf("%s\n", j.dump().c_str());
    } else {
        auto d = resp["data"];
        std::printf("Вошли как %s (%s).\n",
                    d["username"].get<std::string>().c_str(),
                    d["role"].get<std::string>().c_str());
        std::printf("Профиль: %s\n", profile_name.c_str());
    }
    return 0;
}

// --- login local ---

int do_login_local(const AuthOpts& o, bool json_out) {
    std::string lib_name;
    try { lib_name = pick_library(o.library); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    std::string username = o.user.value_or("");
    if (username.empty()) {
        if (json_out) return fail("--user is required in --json mode", json_out);
        std::fprintf(stderr, "Имя пользователя: ");
        std::fflush(stderr);
        std::getline(std::cin, username);
    }
    if (username.empty()) return fail("username is empty", json_out);

    std::string password;
    if (o.password) password = *o.password;
    else if (json_out) return fail("--password is required in --json mode", json_out);
    else password = prompt_password("Пароль: ");

    auto resp = api::login(lib_name, username, password);

    if (!resp.value("ok", false)) {
        return print_login_result(resp, "", json_out);
    }

    std::string profile_name = o.profile.value_or("default");
    std::string token = resp["data"].value("token", "");

    core::Profile p;
    p.name          = profile_name;
    p.library       = lib_name;
    p.username      = username;
    p.mode          = "local";
    p.session_token = token;
    core::put_profile(p);

    return print_login_result(resp, profile_name, json_out);
}

// --- login remote ---

int do_login_remote(const AuthOpts& o, bool json_out) {
    std::string host = o.host.value_or("");
    if (host.empty()) {
        if (json_out) return fail("--host is required in --json mode", json_out);
        host = prompt_line("Хост сервера: ");
    }
    if (host.empty()) return fail("host is empty", json_out);

    int port = o.port.value_or(0);
    if (port == 0) {
        if (json_out) return fail("--port is required in --json mode", json_out);
        std::string s = prompt_line("Порт [8420]: ");
        if (s.empty()) port = 8420;
        else { try { port = std::stoi(s); } catch (...) { return fail("invalid port", json_out); } }
    }

    std::string code = o.code.value_or("");
    if (code.empty()) {
        if (json_out) return fail("--code is required in --json mode", json_out);
        code = prompt_line("Код подключения (8 цифр): ");
    }
    if (code.empty()) return fail("code is empty", json_out);

    std::string username = o.user.value_or("");
    if (username.empty()) {
        if (json_out) return fail("--user is required in --json mode", json_out);
        username = prompt_line("Имя пользователя: ");
    }
    if (username.empty()) return fail("username is empty", json_out);

    std::string password;
    if (o.password) password = *o.password;
    else if (json_out) return fail("--password is required in --json mode", json_out);
    else password = prompt_password("Пароль: ");

    if (password.empty()) return fail("password is empty", json_out);

    try {
        net::HttpClient cli(host, port);

        json login_args;
        login_args["username"] = username;
        login_args["password"] = password;

        json resp = cli.call("login", login_args);

        std::string role, token;

        if (!resp.value("ok", false)) {
            std::string err_code = resp["error"].value("code", "");
            if (err_code == "user_not_found") {
                json reg_args;
                reg_args["code"]     = code;
                reg_args["username"] = username;
                reg_args["password"] = password;

                json reg_resp = cli.call("register", reg_args);
                if (!reg_resp.value("ok", false)) {
                    std::string msg = reg_resp["error"].value("message", "register failed");
                    return fail(msg, json_out, "register_failed");
                }
                role  = reg_resp["data"].value("role", "reader");
                token = reg_resp["data"].value("token", "");
            } else {
                std::string msg = resp["error"].value("message", "login failed");
                return fail(msg, json_out, err_code.c_str());
            }
        } else {
            role  = resp["data"].value("role", "reader");
            token = resp["data"].value("token", "");
        }

        std::string profile_name = o.profile.value_or("default");

        core::Profile p;
        p.name          = profile_name;
        p.library       = "";
        p.username      = username;
        p.mode          = "remote";
        p.host          = host;
        p.port          = port;
        p.code          = code;
        p.session_token = token;
        core::put_profile(p);

        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"]["profile"]  = profile_name;
            j["data"]["host"]     = host;
            j["data"]["port"]     = port;
            j["data"]["username"] = username;
            j["data"]["role"]     = role;
            j["data"]["token"]    = token;
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("Вошли как %s (%s) на %s:%d.\n",
                        username.c_str(), role.c_str(), host.c_str(), port);
            std::printf("Профиль: %s (remote)\n", profile_name.c_str());
        }
        return 0;
    } catch (const std::exception& e) {
        return fail(e.what(), json_out, "network");
    }
}

// --- logout ---

int do_logout(const AuthOpts& o, bool json_out) {
    std::string profile_name = o.profile.value_or("default");

    auto prof = core::get_profile(profile_name);
    if (!prof) return fail("profile not found: " + profile_name, json_out, "profile_not_found");

    if (prof->session_token && prof->mode == "local" && !prof->library.empty()) {
        try {
            api::logout(prof->library, *prof->session_token);
        } catch (...) {}
    }

    auto p2 = *prof;
    p2.session_token.reset();
    core::put_profile(p2);

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["profile"] = profile_name;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Вышли из профиля %s.\n", profile_name.c_str());
    }
    return 0;
}

// --- whoami ---

int do_whoami(const AuthOpts& o, bool json_out) {
    std::string profile_name = o.profile.value_or("");

    if (profile_name.empty()) {
        auto all = core::load_profiles();
        profile_name = all.default_name;
        if (profile_name.empty())
            return fail("no profiles; run `libcore login`", json_out, "no_profile");
    }

    auto prof = core::get_profile(profile_name);
    if (!prof) return fail("profile not found: " + profile_name, json_out, "profile_not_found");

    std::string role = "?";
    bool valid = false;

    if (prof->mode == "remote") {
        if (prof->host && prof->port && prof->session_token) {
            try {
                net::HttpClient cli(*prof->host, *prof->port);
                json resp = cli.call("whoami", json::object(), *prof->session_token);
                if (resp.value("ok", false)) {
                    role  = resp["data"].value("role", "?");
                    valid = true;
                }
            } catch (...) {}
        }
    } else if (prof->session_token && !prof->library.empty()) {
        try {
            auto resp = api::whoami(prof->library, *prof->session_token);
            if (resp.value("ok", false)) {
                role  = resp["data"].value("role", "?");
                valid = true;
            }
        } catch (...) {}
    }

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["profile"]  = prof->name;
        j["data"]["library"]  = prof->library;
        j["data"]["username"] = prof->username;
        j["data"]["role"]     = role;
        j["data"]["mode"]     = prof->mode;
        j["data"]["valid"]    = valid;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Профиль:  %s\n", prof->name.c_str());
        if (prof->mode == "remote") {
            std::printf("Сервер:   %s:%d (remote)\n",
                        prof->host ? prof->host->c_str() : "?",
                        prof->port.value_or(0));
        } else {
            std::printf("Библиотека: %s\n", prof->library.c_str());
        }
        std::printf("Юзер:     %s\n", prof->username.c_str());
        std::printf("Роль:     %s\n", role.c_str());
        if (!valid) std::printf("(сессия неактивна)\n");
    }
    return 0;
}

} // namespace

int cmd_login (const std::vector<std::string>& args, bool json_out) {
    try {
        auto o = parse_opts(args);
        if (o.remote) return do_login_remote(o, json_out);
        return do_login_local(o, json_out);
    } catch (const std::exception& e) { return fail(e.what(), json_out); }
}

int cmd_logout(const std::vector<std::string>& args, bool json_out) {
    try { return do_logout(parse_opts(args), json_out); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }
}

int cmd_whoami(const std::vector<std::string>& args, bool json_out) {
    try { return do_whoami(parse_opts(args), json_out); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }
}

} // namespace libcore::handlers
