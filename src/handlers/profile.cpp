#include "handlers/profile.hpp"

#include "core/library_db.hpp"
#include "core/profiles.hpp"
#include "core/sessions.hpp"
#include "db/database.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <iostream>
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

struct ProfileOpts {
    std::vector<std::string> positional;
    std::optional<std::string> profile;
};

ProfileOpts parse_opts(const std::vector<std::string>& args) {
    ProfileOpts o;
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

bool role_at_least(const std::string& role, const std::string& required) {
    auto rank = [](const std::string& r) -> int {
        if (r == "host") return 3;
        if (r == "moderator") return 2;
        if (r == "reader") return 1;
        return 0;
    };
    return rank(role) >= rank(required);
}

// --- profile show ---

int do_show(const ProfileOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    std::string target = o.positional.empty() ? ctx.username : o.positional[0];
    bool is_self = (target == ctx.username);
    bool is_mod = role_at_least(ctx.role, "moderator");

    auto r = database.query_params(
        "SELECT profile_public, books_public, stats_public "
        "FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (r.empty()) return fail("user not found: " + target, json_out, "not_found");

    bool pp = db::row_int(r[0], 0) != 0;
    bool bp = db::row_int(r[0], 1) != 0;
    bool sp = db::row_int(r[0], 2) != 0;

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["username"]       = target;
        j["data"]["profile_public"] = pp;
        j["data"]["books_public"]   = bp;
        j["data"]["stats_public"]   = sp;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Приватность %s:\n", target.c_str());
        std::printf("  profile: %s\n", pp ? "public" : "private");
        std::printf("  books:   %s\n", bp ? "public" : "private");
        std::printf("  stats:   %s\n", sp ? "public" : "private");
    }
    (void)is_self; (void)is_mod;
    return 0;
}

// --- set flag ---

int set_privacy(const ProfileOpts& o, bool json_out,
                const std::string& field, bool value)
{
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (field != "profile" && field != "books" && field != "stats") {
        return fail("unknown field: " + field, json_out);
    }

    std::string column;
    if (field == "profile") column = "profile_public";
    else if (field == "books") column = "books_public";
    else column = "stats_public";

    database.exec_params(
        "UPDATE users SET " + column + " = ? WHERE username = ?;",
        { db::int_cell(value ? 1 : 0), db::text_cell(ctx.username) }
    );

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["username"] = ctx.username;
        j["data"][field]      = value ? "public" : "private";
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("%s: %s\n", field.c_str(), value ? "public" : "private");
    }
    return 0;
}

} // namespace

int cmd_profile(const std::vector<std::string>& args, bool json_out) {
    if (args.empty()) {
        return fail("usage: libcore profile <show|public|private>", json_out);
    }

    std::string sub = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());

    ProfileOpts o;
    try { o = parse_opts(rest); }
    catch (const std::exception& e) { return fail(e.what(), json_out); }

    if (sub == "show") return do_show(o, json_out);

    if (sub == "public" || sub == "private") {
        if (o.positional.empty()) {
            return fail("usage: libcore profile " + sub + " <profile|books|stats>", json_out);
        }
        std::string field = o.positional[0];
        return set_privacy(o, json_out, field, sub == "public");
    }

    return fail("unknown subcommand: profile " + sub, json_out);
}

} // namespace libcore::handlers
