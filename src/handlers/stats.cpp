#include "handlers/stats.hpp"

#include "core/library_db.hpp"
#include "core/loans.hpp"
#include "core/profiles.hpp"
#include "core/sessions.hpp"
#include "db/database.hpp"
#include "handlers/remote.hpp"

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

        if (!a.empty() && a[0] == '-')
            throw std::runtime_error("unknown flag: " + a);
        o.positional.push_back(a);
    }
    return o;
}

int do_stats(const StatOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (!o.positional.empty()) args["user"] = o.positional[0];
        return remote_call(*prof, "stats", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    std::string target = o.positional.empty() ? ctx.username : o.positional[0];
    bool is_self = (target == ctx.username);

    auto ur = database.query_params(
        "SELECT id, created_at, last_seen_at, stats_public FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (ur.empty()) return fail("user not found: " + target, json_out, "not_found");

    long long user_id      = db::row_int(ur[0], 0);
    long long created_at   = db::row_int(ur[0], 1);
    bool has_ls            = !db::row_is_null(ur[0], 2);
    long long last_seen_at = has_ls ? db::row_int(ur[0], 2) : 0;
    bool stats_public      = db::row_int(ur[0], 3) != 0;

    if (!is_self && ctx.role == "reader" && !stats_public) {
        return fail("stats are private for " + target, json_out, "forbidden");
    }

    auto now = static_cast<long long>(std::time(nullptr));
    auto q = [&](const char* sql, std::vector<db::Cell> params) -> long long {
        auto r = database.query_params(sql, params);
        if (r.empty() || r[0].empty()) return 0;
        return db::row_int(r[0], 0);
    };

    long long total_loans   = q("SELECT COUNT(*) FROM loans WHERE user_id = ?;", { db::int_cell(user_id) });
    long long active_loans  = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NULL;", { db::int_cell(user_id) });
    long long returned      = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NOT NULL;", { db::int_cell(user_id) });
    long long overdue       = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NULL AND enddate IS NOT NULL AND enddate < ?;", { db::int_cell(user_id), db::int_cell(now) });
    long long requests_pend = q("SELECT COUNT(*) FROM requests WHERE user_id = ? AND status = 'pending';", { db::int_cell(user_id) });

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["username"]         = target;
        j["data"]["created_at"]       = created_at;
        j["data"]["last_seen_at"]     = has_ls ? json(last_seen_at) : json(nullptr);
        j["data"]["total_loans"]      = total_loans;
        j["data"]["active_loans"]     = active_loans;
        j["data"]["returned"]         = returned;
        j["data"]["overdue"]          = overdue;
        j["data"]["requests_pending"] = requests_pend;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Статистика %s:\n", target.c_str());
        std::printf("  всего выдач:    %lld\n", total_loans);
        std::printf("  на руках:       %lld\n", active_loans);
        std::printf("  возвращено:     %lld\n", returned);
        std::printf("  просрочено:     %lld\n", overdue);
        std::printf("  запросов:       %lld\n", requests_pend);
    }
    return 0;
}

} // namespace

int cmd_stats(const std::vector<std::string>& args, bool json_out) {
    try { return do_stats(parse_opts(args), json_out); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }
}

} // namespace libcore::handlers
