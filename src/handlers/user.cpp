#include "handlers/user.hpp"

#include "core/library_db.hpp"
#include "core/logs.hpp"
#include "core/profiles.hpp"
#include "core/sessions.hpp"
#include "db/database.hpp"
#include "handlers/remote.hpp"
#include "util/crypto.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <ctime>
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

bool role_at_least(const std::string& role, const std::string& required) {
    auto rank = [](const std::string& r) -> int {
        if (r == "host") return 3;
        if (r == "moderator") return 2;
        if (r == "reader") return 1;
        return 0;
    };
    return rank(role) >= rank(required);
}

// --- user add ---

int do_add(const UserOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        if (o.positional.size() < 2) return fail("usage: user add <name> <password> [--role R]", json_out);
        json args;
        args["username"] = o.positional[0];
        args["password"] = o.positional[1];
        if (o.role) args["role"] = *o.role;
        return remote_call(*prof, "user.add", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (!role_at_least(ctx.role, "host"))
        return fail("requires host role", json_out, "forbidden");

    if (o.positional.size() < 2) {
        return fail("usage: libcore user add <name> <password> [--role R]", json_out);
    }

    std::string username = o.positional[0];
    std::string password = o.positional[1];
    std::string role = o.role.value_or("reader");

    if (role != "reader" && role != "moderator") {
        return fail("role must be 'reader' or 'moderator'", json_out);
    }
    if (username.empty()) return fail("username is empty", json_out);
    if (password.empty()) return fail("password is empty", json_out);

    auto existing = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(username) }
    );
    if (!existing.empty()) {
        return fail("user already exists: " + username, json_out, "already_exists");
    }

    auto pw_hash = util::hash_password(password);
    auto now = static_cast<long long>(std::time(nullptr));

    database.exec_params(
        "INSERT INTO users (username, pw_hash, role, created_at) "
        "VALUES (?, ?, ?, ?);",
        {
            db::text_cell(username),
            db::text_cell(pw_hash),
            db::text_cell(role),
            db::int_cell(now)
        }
    );

    long long new_id = database.last_insert_rowid();

    core::log_info(database, "user.added", ctx.user_id,
                   std::string("{\"username\":\"") + username +
                   "\",\"role\":\"" + role + "\"}");

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["id"]       = new_id;
        j["data"]["username"] = username;
        j["data"]["role"]     = role;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Пользователь создан: %s (роль: %s, id=%lld)\n",
                    username.c_str(), role.c_str(), new_id);
    }
    return 0;
}

// --- user list ---

int do_list(const UserOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        json args;
        if (o.role) args["role"] = *o.role;
        return remote_call(*prof, "user.list", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    bool is_reader = (ctx.role == "reader");
    std::string role_filter = o.role.value_or("");

    std::vector<db::Row> rows;
    if (role_filter.empty()) {
        rows = database.query(
            "SELECT id, username, role, created_at, last_seen_at, "
            "profile_public, books_public, stats_public "
            "FROM users ORDER BY username;"
        );
    } else {
        rows = database.query_params(
            "SELECT id, username, role, created_at, last_seen_at, "
            "profile_public, books_public, stats_public "
            "FROM users WHERE role = ? ORDER BY username;",
            { db::text_cell(role_filter) }
        );
    }

    if (is_reader) {
        std::vector<db::Row> filtered;
        for (const auto& r : rows) {
            bool pub = db::row_int(r, 5) != 0;
            std::string uname = db::row_text(r, 1);
            if (pub || uname == ctx.username) filtered.push_back(r);
        }
        rows = std::move(filtered);
    }

    if (json_out) {
        json arr = json::array();
        for (const auto& r : rows) {
            json one;
            one["id"]       = db::row_int(r, 0);
            one["username"] = db::row_text(r, 1);
            one["role"]     = db::row_text(r, 2);
            arr.push_back(one);
        }
        json j;
        j["ok"] = true;
        j["data"]["users"] = arr;
        std::printf("%s\n", j.dump().c_str());
    } else {
        if (rows.empty()) {
            std::printf("Пользователей нет.\n");
        } else {
            std::printf("Пользователи (%zu):\n", rows.size());
            for (const auto& r : rows) {
                std::printf("  %-20s %s\n",
                            db::row_text(r, 1).c_str(),
                            db::row_text(r, 2).c_str());
            }
        }
    }
    return 0;
}

// --- user details ---

int do_details(const UserOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        if (o.positional.empty()) return fail("usage: user details <user>", json_out);
        json args;
        args["user"] = o.positional[0];
        return remote_call(*prof, "user.details", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (o.positional.empty()) {
        return fail("usage: libcore user details <user>", json_out);
    }
    std::string target = o.positional[0];

    auto r = database.query_params(
        "SELECT id, username, role, created_at, last_seen_at, "
        "profile_public, books_public, stats_public "
        "FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (r.empty()) return fail("user not found: " + target, json_out, "not_found");

    auto& row = r[0];
    bool is_self = (target == ctx.username);
    bool profile_pub = db::row_int(row, 5) != 0;

    if (ctx.role == "reader" && !is_self && !profile_pub) {
        return fail("profile is private: " + target, json_out, "forbidden");
    }

    auto created   = db::row_int(row, 3);
    bool has_ls    = !db::row_is_null(row, 4);
    auto last_seen = has_ls ? db::row_int(row, 4) : 0;

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["id"]             = db::row_int(row, 0);
        j["data"]["username"]       = db::row_text(row, 1);
        j["data"]["role"]           = db::row_text(row, 2);
        j["data"]["created_at"]     = created;
        j["data"]["last_seen_at"]   = has_ls ? json(last_seen) : json(nullptr);
        j["data"]["profile_public"] = profile_pub;
        j["data"]["books_public"]   = db::row_int(row, 6) != 0;
        j["data"]["stats_public"]   = db::row_int(row, 7) != 0;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Пользователь: %s\n", target.c_str());
        std::printf("  id:         %lld\n", db::row_int(row, 0));
        std::printf("  роль:       %s\n", db::row_text(row, 2).c_str());
        std::printf("  создан:     %lld\n", created);
        if (has_ls) std::printf("  был:        %lld\n", last_seen);
        std::printf("  приватность:\n");
        std::printf("    profile: %s\n", profile_pub ? "public" : "private");
        std::printf("    books:   %s\n", db::row_int(row, 6) ? "public" : "private");
        std::printf("    stats:   %s\n", db::row_int(row, 7) ? "public" : "private");
    }
    return 0;
}

// --- user level ---

int do_level(const UserOpts& o, bool json_out) {
    auto ctx_opt = load_ctx(o.profile);
    if (!ctx_opt) return fail("no profile; run `libcore login`", json_out, "no_profile");
    auto ctx = *ctx_opt;

    auto prof = core::get_profile(ctx.profile_name);
    if (!prof) return fail("profile not found", json_out, "profile_not_found");

    if (prof->mode == "remote") {
        if (o.positional.size() < 2) return fail("usage: user level <user> <reader|moderator>", json_out);
        json args;
        args["user"] = o.positional[0];
        args["role"] = o.positional[1];
        return remote_call(*prof, "user.level", args, json_out);
    }

    auto database = core::open_library_db(ctx.library);
    if (!resolve_session(ctx, database))
        return fail("session not active; run `libcore login`", json_out, "no_session");

    if (!role_at_least(ctx.role, "host"))
        return fail("requires host role", json_out, "forbidden");

    if (o.positional.size() < 2) {
        return fail("usage: libcore user level <user> <reader|moderator>", json_out);
    }

    std::string target = o.positional[0];
    std::string new_role = o.positional[1];

    if (new_role != "reader" && new_role != "moderator") {
        return fail("role must be 'reader' or 'moderator'", json_out);
    }
    if (target == ctx.username) {
        return fail("cannot change your own role", json_out, "self_change");
    }

    auto r = database.query_params(
        "SELECT id, role FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(target) }
    );
    if (r.empty()) return fail("user not found: " + target, json_out, "not_found");

    std::string current_role = db::row_text(r[0], 1);
    if (current_role == "host") {
        return fail("cannot change host role", json_out, "host_change");
    }

    database.exec_params(
        "UPDATE users SET role = ? WHERE username = ?;",
        { db::text_cell(new_role), db::text_cell(target) }
    );

    core::log_info(database, "user.level_changed", ctx.user_id,
                   std::string("{\"username\":\"") + target +
                   "\",\"old\":\"" + current_role +
                   "\",\"new\":\"" + new_role + "\"}");

    if (json_out) {
        json j;
        j["ok"] = true;
        j["data"]["username"] = target;
        j["data"]["old_role"] = current_role;
        j["data"]["new_role"] = new_role;
        std::printf("%s\n", j.dump().c_str());
    } else {
        std::printf("Роль %s: %s → %s\n",
                    target.c_str(), current_role.c_str(), new_role.c_str());
    }
    return 0;
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
