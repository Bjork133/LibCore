#include "handlers/profile.hpp"

#include "api/common.hpp"
#include "api/profile.hpp"
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

struct ProfileOpts {
    std::vector<std::string> positional;
    std::optional<std::string> profile;
};

ProfileOpts parse_opts(const std::vector<std::string>& args) {
    ProfileOpts o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];

        auto take = [&](std::string_view flag) -> std::optional<std::string> {
            if (a.size() > flag.size() && a.starts_with(flag) && a[flag.size()] == '=')
                return a.substr(flag.size() + 1);
            if (a == flag) {
                if (i + 1 >= args.size())
                    throw std::runtime_error(std::string("flag ") + std::string(flag) + " requires a value");
                return args[++i];
            }
            return std::nullopt;
        };

        if (auto v = take("--profile")) { o.profile = *v; continue; }

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
        o.positional.push_back(a);
    }
    return o;
}

int fail(const std::string& msg, bool json_out, const char* code = "profile_error") {
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

int do_show(const ProfileOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto c = *c_opt;

    json args;
    if (!o.positional.empty()) args["user"] = o.positional[0];

    json resp;
    if (c.mode == "remote") {
        try {
            auto p = profile_from_ctx(c);
            resp = remote_raw(p, "profile.show", args);
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"]["code"] = "network";
            j["error"]["message"] = e.what();
            resp = j;
        }
    } else {
        std::optional<std::string> user;
        if (args.contains("user")) user = args["user"].get<std::string>();
        resp = api::profile_show(c.library, c.token, user);
    }

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
    std::printf("Приватность %s:\n", d["username"].get<std::string>().c_str());
    std::printf("  profile: %s\n", d["profile_public"].get<bool>() ? "public" : "private");
    std::printf("  books:   %s\n", d["books_public"].get<bool>() ? "public" : "private");
    std::printf("  stats:   %s\n", d["stats_public"].get<bool>() ? "public" : "private");
    return 0;
}

int set_privacy(const ProfileOpts& o, bool json_out,
                const std::string& field, bool value) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto c = *c_opt;

    json args;
    args["field"] = field;
    args["value"] = value;

    json resp;
    if (c.mode == "remote") {
        try {
            auto p = profile_from_ctx(c);
            resp = remote_raw(p, "profile.set", args);
        } catch (const std::exception& e) {
            json j;
            j["ok"] = false;
            j["error"]["code"] = "network";
            j["error"]["message"] = e.what();
            resp = j;
        }
    } else {
        resp = api::profile_set(c.library, c.token, field, value);
    }

    if (!resp.value("ok", false)) {
        std::string msg = resp["error"].value("message", "");
        if (json_out) std::fprintf(stderr, "%s\n", resp.dump().c_str());
        else          std::fprintf(stderr, "libcore: %s\n", msg.c_str());
        return 1;
    }

    if (json_out) {
        std::printf("%s\n", resp.dump().c_str());
    } else {
        std::printf("%s: %s\n", field.c_str(), value ? "public" : "private");
    }
    return 0;
}

int do_use(const ProfileOpts& o, bool json_out) {
    auto all = core::load_profiles();

    if (o.positional.empty()) {
        if (all.default_name.empty())
            return fail("no default profile; use `profile use <name>`", json_out, "no_default");
        if (json_out) {
            json j;
            j["ok"] = true;
            j["data"]["default"] = all.default_name;
            std::printf("%s\n", j.dump().c_str());
        } else {
            std::printf("%s\n", all.default_name.c_str());
        }
        return 0;
    }

    std::string name = o.positional[0];
    bool found = false;
    for (const auto& p : all.all) {
        if (p.name == name) { found = true; break; }
    }
    if (!found) return fail("profile not found: " + name, json_out, "not_found");

    core::set_default_profile(name);

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["default"] = name;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Активный профиль: %s\n", name.c_str());
    }
    return 0;
}

int do_list(bool json_out) {
    auto all = core::load_profiles();

    if (json_out) {
        json arr = json::array();
        for (const auto& p : all.all) {
            json one;
            one["name"]    = p.name;
            one["mode"]    = p.mode;
            one["user"]    = p.username;
            one["default"] = (p.name == all.default_name);
            if (p.mode == "remote") {
                one["host"] = p.host ? json(*p.host) : json(nullptr);
                one["port"] = p.port ? json(*p.port) : json(nullptr);
            } else {
                one["library"] = p.library;
            }
            arr.push_back(one);
        }
        json j;
        j["ok"] = true;
        j["data"]["profiles"] = arr;
        j["data"]["default"]  = all.default_name;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (all.all.empty()) { std::printf("Профилей нет.\n"); return 0; }
        for (const auto& p : all.all) {
            std::string marker = (p.name == all.default_name) ? " *" : "";
            if (p.mode == "remote") {
                std::printf("  %s%s  (remote: %s:%d, %s)\n",
                            p.name.c_str(), marker.c_str(),
                            p.host ? p.host->c_str() : "?",
                            p.port.value_or(0),
                            p.username.c_str());
            } else {
                std::printf("  %s%s  (local: %s, %s)\n",
                            p.name.c_str(), marker.c_str(),
                            p.library.c_str(), p.username.c_str());
            }
        }
    }
    return 0;
}

} // namespace

int cmd_profile(const std::vector<std::string>& args, bool json_out) {
    if (args.empty())
        return fail("usage: libcore profile <show|public|private|use|list>", json_out);

    std::string sub = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    ProfileOpts o;
    try { o = parse_opts(rest); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    if (sub == "show") return do_show(o, json_out);
    if (sub == "use")  return do_use(o, json_out);
    if (sub == "list") return do_list(json_out);

    if (sub == "public" || sub == "private") {
        if (o.positional.empty())
            return fail("usage: libcore profile " + sub + " <profile|books|stats>", json_out);
        std::string field = o.positional[0];
        return set_privacy(o, json_out, field, sub == "public");
    }

    return fail("unknown subcommand: profile " + sub, json_out);
}

} // namespace libcore::handlers
