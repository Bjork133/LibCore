#include "handlers/user.hpp"

#include "api/common.hpp"
#include "api/user.hpp"
#include "core/profiles.hpp"
#include "handlers/remote.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>

namespace libcore::handlers {

using json = nlohmann::json;

namespace {

struct Ctx {
    std::string profile_name;
    std::string library;
    std::string username;
    std::string token;
    std::string mode;
    std::optional<std::string> host;
    std::optional<int> port;
    std::optional<std::string> code;
};

std::optional<Ctx> load_ctx(const std::optional<std::string>& explicit_profile) {
    std::string profile_name = explicit_profile.value_or("");
    auto all = core::load_profiles();
    if (profile_name.empty()) profile_name = all.default_name;
    if (profile_name.empty()) return std::nullopt;

    auto prof = core::get_profile(profile_name);
    if (!prof) return std::nullopt;

    Ctx c;
    c.profile_name = prof->name;
    c.library      = prof->library;
    c.username     = prof->username;
    c.token        = prof->session_token.value_or("");
    c.mode         = prof->mode;
    c.host         = prof->host;
    c.port         = prof->port;
    c.code         = prof->code;
    return c;
}

core::Profile profile_from_ctx(const Ctx& c) {
    core::Profile p;
    p.name          = c.profile_name;
    p.library       = c.library;
    p.username      = c.username;
    p.mode          = c.mode;
    p.host          = c.host;
    p.port          = c.port;
    p.code          = c.code;
    if (!c.token.empty()) p.session_token = c.token;
    return p;
}

struct UserOpts {
    std::vector<std::string> positional;
    std::optional<std::string> profile;
    std::optional<std::string> role;
};

UserOpts parse_opts(const std::vector<std::string>& args) {
    UserOpts o;
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

        if (auto v = take("--profile")) { o.profile = *v; continue; }
        if (auto v = take("--role"))    { o.role    = *v; continue; }

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
        o.positional.push_back(a);
    }
    return o;
}

int fail(const std::string& msg, bool json_out, const char* code = "user_error") {
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

json call_api(const Ctx& c, const std::string& cmd, const json& args) {
    if (c.mode == "remote") {
        try {
            auto p = profile_from_ctx(c);
            return remote_raw(p, cmd, args);
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"]["code"] = "network";
            j["error"]["message"] = e.what();
            return j;
        }
    }
    if (cmd == "user.list") {
        std::optional<std::string> role;
        if (args.contains("role") && !args["role"].is_null())
            role = args["role"].get<std::string>();
        return api::user_list(c.library, c.token, role);
    }
    if (cmd == "user.details")
        return api::user_details(c.library, c.token, args["user"].get<std::string>());
    if (cmd == "user.add")
        return api::user_add(c.library, c.token,
            args["username"].get<std::string>(),
            args["password"].get<std::string>(),
            args.value("role", "reader"));
    if (cmd == "user.level")
        return api::user_level(c.library, c.token,
            args["user"].get<std::string>(),
            args["role"].get<std::string>());

    json j;
    j["ok"] = false;
    j["error"]["code"] = "unknown_cmd";
    j["error"]["message"] = cmd;
    return j;
}

int print_result(const json& resp, bool json_out) {
    if (!resp.value("ok", false)) {
        std::string msg = resp["error"].value("message", "");
        if (json_out) std::fprintf(stderr, "%s\n", resp.dump().c_str());
        else          std::fprintf(stderr, "libcore: %s\n", msg.c_str());
        return 1;
    }
    if (json_out) {
        std::printf("%s\n", resp.dump().c_str());
        return 0;
    }
    auto d = resp["data"];
    if (d.contains("users")) {
        auto& arr = d["users"];
        if (arr.empty()) std::printf("Пользователей нет.\n");
        else {
            std::printf("Пользователи (%zu):\n", arr.size());
            for (const auto& u : arr) {
                std::printf("  %-20s %s\n",
                            u["username"].get<std::string>().c_str(),
                            u["role"].get<std::string>().c_str());
            }
        }
    } else if (d.contains("created_at")) {
        std::printf("Пользователь: %s\n", d["username"].get<std::string>().c_str());
        std::printf("  id:         %lld\n", d["id"].get<long long>());
        std::printf("  роль:       %s\n", d["role"].get<std::string>().c_str());
        std::printf("  создан:     %lld\n", d["created_at"].get<long long>());
        if (!d["last_seen_at"].is_null())
            std::printf("  был:        %lld\n", d["last_seen_at"].get<long long>());
        std::printf("  приватность:\n");
        std::printf("    profile: %s\n", d["profile_public"].get<bool>() ? "public" : "private");
        std::printf("    books:   %s\n", d["books_public"].get<bool>() ? "public" : "private");
        std::printf("    stats:   %s\n", d["stats_public"].get<bool>() ? "public" : "private");
    } else if (d.contains("id") && d.contains("role")) {
        std::printf("Пользователь создан: %s (роль: %s, id=%lld)\n",
                    d["username"].get<std::string>().c_str(),
                    d["role"].get<std::string>().c_str(),
                    d["id"].get<long long>());
    } else if (d.contains("old_role") && d.contains("new_role")) {
        std::printf("Роль %s: %s → %s\n",
                    d["username"].get<std::string>().c_str(),
                    d["old_role"].get<std::string>().c_str(),
                    d["new_role"].get<std::string>().c_str());
    } else {
        std::printf("%s\n", d.dump(2).c_str());
    }
    return 0;
}

int do_add(const UserOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.size() < 2) return fail("usage: user add <name> <password> [--role R]", json_out);

    json args;
    args["username"] = o.positional[0];
    args["password"] = o.positional[1];
    if (o.role) args["role"] = *o.role;

    auto resp = call_api(*c_opt, "user.add", args);
    return print_result(resp, json_out);
}

int do_list(const UserOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");

    json args;
    if (o.role) args["role"] = *o.role;
    auto resp = call_api(*c_opt, "user.list", args);
    return print_result(resp, json_out);
}

int do_details(const UserOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.empty()) return fail("usage: user details <user>", json_out);

    json args;
    args["user"] = o.positional[0];
    auto resp = call_api(*c_opt, "user.details", args);
    return print_result(resp, json_out);
}

int do_level(const UserOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    if (o.positional.size() < 2) return fail("usage: user level <user> <reader|moderator>", json_out);

    json args;
    args["user"] = o.positional[0];
    args["role"] = o.positional[1];
    auto resp = call_api(*c_opt, "user.level", args);
    return print_result(resp, json_out);
}

} // namespace

int cmd_user(const std::vector<std::string>& args, bool json_out) {
    if (args.empty()) return fail("usage: libcore user <add|list|details|level>", json_out);

    std::string sub = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    UserOpts o;
    try { o = parse_opts(rest); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    if (sub == "add")     return do_add(o, json_out);
    if (sub == "list")    return do_list(o, json_out);
    if (sub == "details") return do_details(o, json_out);
    if (sub == "level")   return do_level(o, json_out);

    return fail("unknown subcommand: user " + sub, json_out);
}

} // namespace libcore::handlers
