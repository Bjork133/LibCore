#include "api/auth.hpp"

#include "api/library.hpp"
#include "core/logs.hpp"
#include "core/server_meta.hpp"
#include "core/sessions.hpp"
#include "util/crypto.hpp"

#include <ctime>

namespace libcore::api {

json login(const std::string& library,
           const std::string& username,
           const std::string& password) {
    try {
        auto database = open_library(library);

        auto r = database.query_params(
            "SELECT id, pw_hash, role FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(username) }
        );
        if (r.empty()) {
            return err("user_not_found", "user not found: " + username);
        }

        long long user_id = db::row_int(r[0], 0);
        std::string pw_hash = db::row_text(r[0], 1);
        std::string role    = db::row_text(r[0], 2);

        if (!util::verify_password(password, pw_hash)) {
            core::log_warn(database, "auth.failed", user_id,
                           std::string("{\"username\":\"") + username +
                           "\",\"reason\":\"invalid_password\"}");
            return err("invalid_password", "invalid password");
        }

        core::delete_user_sessions(database, user_id);
        auto token = core::create_session(database, user_id, 30LL * 24 * 3600);

        database.exec_params(
            "UPDATE users SET last_seen_at = ? WHERE id = ?;",
            { db::int_cell(static_cast<long long>(std::time(nullptr))),
              db::int_cell(user_id) }
        );

        core::log_info(database, "user.login", user_id,
                       std::string("{\"username\":\"") + username + "\"}");

        json d;
        d["username"] = username;
        d["role"]     = role;
        d["token"]    = token;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json register_user(const std::string& library,
                   const std::string& code,
                   const std::string& username,
                   const std::string& password) {
    try {
        auto meta = core::read_meta(library);
        if (!meta) return err("no_meta", "server.json missing");

        if (code != meta->code) {
            return err("bad_code", "invalid library code");
        }

        if (username.empty() || password.empty()) {
            return err("bad_request", "username/password empty");
        }

        auto database = open_library(library);

        auto existing = database.query_params(
            "SELECT id FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(username) }
        );
        if (!existing.empty()) {
            return err("already_exists", "user already exists: " + username);
        }

        auto pw_hash = util::hash_password(password);
        auto now = static_cast<long long>(std::time(nullptr));

        database.exec_params(
            "INSERT INTO users (username, pw_hash, role, created_at) "
            "VALUES (?, ?, 'reader', ?);",
            {
                db::text_cell(username),
                db::text_cell(pw_hash),
                db::int_cell(now)
            }
        );

        long long user_id = database.last_insert_rowid();

        core::log_info(database, "user.registered", user_id,
                       std::string("{\"username\":\"") + username + "\"}");

        auto token = core::create_session(database, user_id, 30LL * 24 * 3600);

        json d;
        d["username"] = username;
        d["role"]     = "reader";
        d["token"]    = token;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json whoami(const std::string& library, const std::string& token) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        json d;
        d["username"] = ctx->username;
        d["role"]     = ctx->role;
        d["user_id"]  = ctx->user_id;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json logout(const std::string& library, const std::string& token) {
    try {
        auto database = open_library(library);
        core::delete_session(database, token);
        return ok();
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

} // namespace libcore::api
