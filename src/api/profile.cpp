#include "api/profile.hpp"

#include "api/library.hpp"

namespace libcore::api {

json profile_show(const std::string& library, const std::string& token,
                  const std::optional<std::string>& target) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        std::string uname = target.value_or(ctx->username);

        auto r = database.query_params(
            "SELECT profile_public, books_public, stats_public "
            "FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(uname) }
        );
        if (r.empty()) return err("not_found", "user not found: " + uname);

        json d;
        d["username"]       = uname;
        d["profile_public"] = db::row_int(r[0], 0) != 0;
        d["books_public"]   = db::row_int(r[0], 1) != 0;
        d["stats_public"]   = db::row_int(r[0], 2) != 0;
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

json profile_set(const std::string& library, const std::string& token,
                 const std::string& field,
                 bool value) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        std::string column;
        if (field == "profile") column = "profile_public";
        else if (field == "books") column = "books_public";
        else if (field == "stats") column = "stats_public";
        else return err("bad_request", "unknown field: " + field);

        database.exec_params(
            "UPDATE users SET " + column + " = ? WHERE username = ?;",
            { db::int_cell(value ? 1 : 0), db::text_cell(ctx->username) }
        );

        json d;
        d["username"] = ctx->username;
        d[field]      = value ? "public" : "private";
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

} // namespace libcore::api
