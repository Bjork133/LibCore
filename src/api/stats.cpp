#include "api/stats.hpp"

#include "api/library.hpp"

#include <ctime>

namespace libcore::api {

json stats(const std::string& library, const std::string& token,
           const std::optional<std::string>& target) {
    try {
        auto database = open_library(library);
        auto ctx = auth_by_token(database, token);
        if (!ctx) return err("unauthorized", "invalid or missing token");

        std::string uname = target.value_or(ctx->username);
        bool is_self = (uname == ctx->username);

        auto ur = database.query_params(
            "SELECT id, created_at, last_seen_at, stats_public FROM users WHERE username = ? LIMIT 1;",
            { db::text_cell(uname) }
        );
        if (ur.empty()) return err("not_found", "user not found: " + uname);

        long long user_id      = db::row_int(ur[0], 0);
        long long created_at   = db::row_int(ur[0], 1);
        bool has_ls            = !db::row_is_null(ur[0], 2);
        long long last_seen_at = has_ls ? db::row_int(ur[0], 2) : 0;
        bool stats_public      = db::row_int(ur[0], 3) != 0;

        if (!is_self && ctx->role == "reader" && !stats_public) {
            return err("forbidden", "stats are private for " + uname);
        }

        auto now = static_cast<long long>(std::time(nullptr));
        auto q = [&](const char* sql, std::vector<db::Cell> params) -> long long {
            auto r = database.query_params(sql, params);
            if (r.empty() || r[0].empty()) return 0;
            return db::row_int(r[0], 0);
        };

        json d;
        d["username"]         = uname;
        d["created_at"]       = created_at;
        d["last_seen_at"]     = has_ls ? json(last_seen_at) : json(nullptr);
        d["total_loans"]      = q("SELECT COUNT(*) FROM loans WHERE user_id = ?;", { db::int_cell(user_id) });
        d["active_loans"]     = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NULL;", { db::int_cell(user_id) });
        d["returned"]         = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NOT NULL;", { db::int_cell(user_id) });
        d["overdue"]          = q("SELECT COUNT(*) FROM loans WHERE user_id = ? AND returndate IS NULL AND enddate IS NOT NULL AND enddate < ?;", { db::int_cell(user_id), db::int_cell(now) });
        d["requests_pending"] = q("SELECT COUNT(*) FROM requests WHERE user_id = ? AND status = 'pending';", { db::int_cell(user_id) });
        return ok(d);
    } catch (const std::exception& e) {
        return err("internal", e.what());
    }
}

} // namespace libcore::api
