#include "handlers/logs.hpp"

#include "core/library_db.hpp"
#include "core/logs.hpp"
#include "core/profiles.hpp"
#include "core/sessions.hpp"
#include "core/settings.hpp"
#include "db/database.hpp"
#include "util/dateparse.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <ctime>
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
    std::string role;
    long long   user_id = 0;
    std::optional<std::string> session_token;
};

std::optional<Ctx> load_ctx(const std::optional<std::string>& explicit_profile) {
    std::string profile_name = explicit_profile.value_or("");
    auto all = core::load_profiles();
    if (profile_name.empty()) profile_name = all.default_name;
    if (profile_name.empty()) return std::nullopt;

    auto prof = core::get_profile(profile_name);
    if (!prof) return std::nullopt;

    Ctx c;
    c.profile_name  = prof->name;
    c.library       = prof->library;
    c.username      = prof->username;
    c.session_token = prof->session_token;
    return c;
}

bool resolve_session(Ctx& c, db::Database& database) {
    if (!c.session_token) return false;
    auto s = core::get_session(database, *c.session_token);
    if (!s) return false;
    c.role = s->role;
    auto r = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(c.username) }
    );
    if (r.empty()) return false;
    c.user_id = db::row_int(r[0], 0);
    return true;
}

struct LogOpts {
    std::optional<std::string> profile;
    std::optional<std::string> level;
    std::optional<std::string> event;
    std::optional<std::string> since;
    int tail = 50;
};

LogOpts parse_opts(const std::vector<std::string>& args) {
    LogOpts o;
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
        if (auto v = take("--level"))   { o.level   = *v; continue; }
        if (auto v = take("--event"))   { o.event   = *v; continue; }
        if (auto v = take("--since"))   { o.since   = *v; continue; }
        if (auto v = take("--tail")) {
            try { o.tail = std::stoi(*v); }
            catch (...) { throw std::runtime_error("invalid --tail: " + *v); }
            continue;
        }

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
    }
    return o;
}

int fail(const std::string& msg, bool json_out, const char* code = "logs_error") {
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

bool role_at_least(const std::string& role, const std::string& required) {
    auto rank = [](const std::string& r) -> int {
        if (r == "host") return 3;
        if (r == "moderator") return 2;
        if (r == "reader") return 1;
        return 0;
    };
    return rank(role) >= rank(required);
}

} // namespace

int cmd_logs(const std::vector<std::string>& args, bool json_out) {
    LogOpts o;
    try { o = parse_opts(args); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (!role_at_least(ctx.role, "moderator"))
        return fail("requires moderator role or higher", json_out, "forbidden");

    std::optional<long long> since_ts;
    if (o.since) {
        auto s = core::load_settings();
        auto ts = util::parse_date(*o.since, s.time_format);
        if (!ts) return fail("invalid --since date (expected " + s.time_format + ")", json_out);
        since_ts = *ts;
    }

    auto entries = core::list_logs(database, o.level, o.event, since_ts, o.tail);

    if (json_out) {
        json arr = json::array();
        for (const auto& l : entries) {
            json one;
            one["id"]     = l.id;
            one["ts"]     = l.ts;
            one["level"]  = l.level;
            one["event"]  = l.event;
            one["actor"]  = l.actor_id ? json(*l.actor_id) : json(nullptr);
            one["payload"] = l.payload ? json(*l.payload) : json(nullptr);
            arr.push_back(one);
        }
        json j;
        j["ok"] = true;
        j["data"]["logs"] = arr;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (entries.empty()) {
            std::printf("Логов нет.\n");
        } else {
            auto s = core::load_settings();
            for (const auto& l : entries) {
                // Формат: 2026-09-27 10:12:03  INFO   user.login
                std::string ts = util::format_date(l.ts, s.time_format);
                std::printf("%s  %-5s  %-20s",
                            ts.c_str(),
                            l.level.c_str(),
                            l.event.c_str());
                if (l.payload) std::printf("  %s", l.payload->c_str());
                std::printf("\n");
            }
        }
    }
    return 0;
}

} // namespace libcore::handlers
