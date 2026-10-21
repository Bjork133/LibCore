#pragma once

#include <optional>
#include <string>

namespace libcore::db { class Database; }

namespace libcore::core {

struct Session {
    std::string token;
    std::string username;
    std::string role;
    long long   created_at = 0;
    long long   expires_at = 0;
};

std::string create_session(db::Database& db, long long user_id,
                           long long ttl_seconds);

std::optional<Session> get_session(db::Database& db, const std::string& token);

void delete_session(db::Database& db, const std::string& token);

void delete_user_sessions(db::Database& db, long long user_id);

std::string generate_token();

} // namespace libcore::core
