#include "api/user.hpp"

#include "api/library.hpp"
#include "core/logs.hpp"
#include "util/crypto.hpp"

#include <ctime>

namespace libcore::api {

json user_list(const std::string& library, const std::string& token,
               const std::optional<std::string>& role_filter) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        std::vector<db::Row> rows;
        if (!role_filter || role_filter->empty()) {
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
                { db::text_cell(*role_filter) }
            );
        }

        if (ctx->role == "reader") {
            std::vector<db::Row> filtered;
            for (const auto& r : rows) {
                bool pub = db::row_int(r, 5) != 0;
                std::string uname = db::row_text(r, 1);
                if (pub || uname == ctx->username) filtered.push_back(r);
            }
            rows = std::move(filtered);
        }

        json arr = json::array();
        for (const auto& r : rows) {
            json one;
            one["id"]       = db::row_int(r, 0);
            one["username"] = db::row_text(r, 1);
            one["role"]     = db::row_text(r, 2);
            arr.push_back(one);
        }
        json d;
        d["users"] = arr;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json user_details(const std::string& library, const std::string& token,
                  const std::string& target) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        auto r = database.query_params(
            "SELECT id, username, role, created_at, last_seen_at, "
            "profile_public, books_public, stats_public "
            "FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(target) }
        );
        if (r.empty()) return err("not_found", "user not found: " + target);

        auto& row = r[0];
        bool is_self = (target == ctx->username);
        bool profile_pub = db::row_int(row, 5) != 0;

        if (ctx->role == "reader" && !is_self && !profile_pub) {
            return err("forbidden", "profile is private: " + target);
        }

        auto created   = db::row_int(row, 3);
        bool has_ls    = !db::row_is_null(row, 4);
        auto last_seen = has_ls ? db::row_int(row, 4) : 0;

        json d;
        d["id"]             = db::row_int(row, 0);
        d["username"]       = db::row_text(row, 1);
        d["role"]           = db::row_text(row, 2);
        d["created_at"]     = created;
        d["last_seen_at"]   = has_ls ? json(last_seen) : json(nullptr);
        d["profile_public"] = profile_pub;
        d["books_public"]   = db::row_int(row, 6) != 0;
        d["stats_public"]   = db::row_int(row, 7) != 0;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json user_add(const std::string& library, const std::string& token,
              const std::string& username,
              const std::string& password,
              const std::string& role) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        if (!role_at_least(ctx->role, "host"))
            return err("forbidden", "requires host role");

        if (role != "reader" && role != "moderator")
            return err("bad_request", "role must be reader or moderator");

        if (username.empty() || password.empty())
            return err("bad_request", "username/password empty");

        auto existing = database.query_params(
            "SELECT id FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(username) }
        );
        if (!existing.empty())
            return err("already_exists", "user already exists: " + username);

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

        core::log_info(database, "user.added", ctx->user_id,
                       std::string("{\"username\":\"") + username +
                       "\",\"role\":\"" + role + "\"}");

        json d;
        d["id"]       = new_id;
        d["username"] = username;
        d["role"]     = role;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json user_level(const std::string& library, const std::string& token,
                const std::string& target,
                const std::string& new_role) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        if (!role_at_least(ctx->role, "host"))
            return err("forbidden", "requires host role");

        if (new_role != "reader" && new_role != "moderator")
            return err("bad_request", "role must be reader or moderator");

        if (target == ctx->username)
            return err("self_change", "cannot change your own role");

        auto r = database.query_params(
            "SELECT id, role FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(target) }
        );
        if (r.empty()) return err("not_found", "user not found: " + target);

        std::string current_role = db::row_text(r[0], 1);
        if (current_role == "host")
            return err("host_change", "cannot change host role");

        database.exec_params(
            "UPDATE users SET role = ? WHERE username = ?;",
            { db::text_cell(new_role), db::text_cell(target) }
        );

        core::log_info(database, "user.level_changed", ctx->user_id,
                       std::string("{\"username\":\"") + target +
                       "\",\"old\":\"" + current_role +
                       "\",\"new\":\"" + new_role + "\"}");

        json d;
        d["username"] = target;
        d["old_role"] = current_role;
        d["new_role"] = new_role;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

} // namespace libcore::api
