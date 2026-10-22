#include "handlers/stats.hpp"

#include "api/common.hpp"
#include "api/stats.hpp"
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

int fail(const std::string& msg, bool json_out, const char* code = "stats_error") {
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

struct StatOpts {
    std::vector<std::string> positional;
    std::optional<std::string> profile;
};

StatOpts parse_opts(const std::vector<std::string>& args) {
    StatOpts o;
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

int do_stats(const StatOpts& o, bool json_out) {
    auto c_opt = load_ctx(o.profile);
    if (!c_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto c = *c_opt;

    json args;
    if (!o.positional.empty()) args["user"] = o.positional[0];

    json resp;
    if (c.mode == "remote") {
        try {
            auto p = profile_from_ctx(c);
            resp = remote_raw(p, "stats", args);
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
        resp = api::stats(c.library, c.token, user);
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
    std::printf("Статистика %s:\n", d["username"].get<std::string>().c_str());
    std::printf("  всего выдач:    %lld\n", d["total_loans"].get<long long>());
    std::printf("  на руках:       %lld\n", d["active_loans"].get<long long>());
    std::printf("  возвращено:     %lld\n", d["returned"].get<long long>());
    std::printf("  просрочено:     %lld\n", d["overdue"].get<long long>());
    std::printf("  запросов:       %lld\n", d["requests_pending"].get<long long>());
    return 0;
}

} // namespace

int cmd_stats(const std::vector<std::string>& args, bool json_out) {
    try { return do_stats(parse_opts(args), json_out); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }
}

} // namespace libcore::handlers
