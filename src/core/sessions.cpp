#include "core/sessions.hpp"

#include "db/database.hpp"

#include <random>
#include <ctime>
#include <format>

namespace libcore::core {

namespace {

std::string random_hex(std::size_t n_bytes) {
    std::random_device rd;
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(n_bytes * 2);
    for (std::size_t i = 0; i < n_bytes; ++i) {
        uint8_t b = static_cast<uint8_t>(rd() & 0xFF);
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0F]);
    }
    return out;
}

} // namespace

std::string generate_token() {
    return random_hex(32);   // 64 hex-символа
}

std::string create_session(db::Database& database, long long user_id,
                           long long ttl_seconds) {
    auto token = generate_token();
    auto now   = static_cast<long long>(std::time(nullptr));
    auto expires = now + ttl_seconds;

    database.exec_params(
        "INSERT INTO sessions (token, user_id, created_at, expires_at) "
        "VALUES (?, ?, ?, ?);",
        {
            db::text_cell(token),
            db::int_cell(user_id),
            db::int_cell(now),
            db::int_cell(expires)
        }
    );
    return token;
}

std::optional<Session> get_session(db::Database& database, const std::string& token) {
    auto r = database.query_params(
        "SELECT s.token, u.username, u.role, s.created_at, s.expires_at "
        "FROM sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token = ?;",
        { db::text_cell(token) }
    );
    if (r.empty()) return std::nullopt;

    auto now = static_cast<long long>(std::time(nullptr));
    Session s;
    s.token      = db::row_text(r[0], 0);
    s.username   = db::row_text(r[0], 1);
    s.role       = db::row_text(r[0], 2);
    s.created_at = db::row_int (r[0], 3);
    s.expires_at = db::row_int (r[0], 4);

    if (s.expires_at <= now) {
        // сессия истекла — удаляем
        delete_session(database, token);
        return std::nullopt;
    }
    return s;
}

void delete_session(db::Database& database, const std::string& token) {
    database.exec_params("DELETE FROM sessions WHERE token = ?;",
                         { db::text_cell(token) });
}

void delete_user_sessions(db::Database& database, long long user_id) {
    database.exec_params("DELETE FROM sessions WHERE user_id = ?;",
                         { db::int_cell(user_id) });
}

} // namespace libcore::core
