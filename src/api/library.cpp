#include "api/library.hpp"

#include "core/library_db.hpp"
#include "core/sessions.hpp"
#include "db/migrations.hpp"

namespace libcore::api {

db::Database open_library(const std::string& library) {
    return core::open_library_db(library);
}

std::optional<Context> auth_by_token(db::Database& database, const std::string& token) {
    if (token.empty()) return std::nullopt;
    auto s = core::get_session(database, token);
    if (!s) return std::nullopt;

    auto r = database.query_params(
        "SELECT id FROM users WHERE username = ? LIMIT 1;",
        { db::text_cell(s->username) }
    );
    if (r.empty()) return std::nullopt;

    Context c;
    c.user_id  = db::row_int(r[0], 0);
    c.username = s->username;
    c.role     = s->role;
    return c;
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

} // namespace libcore::api
