#pragma once

#include "db/database.hpp"

#include <optional>
#include <string>
#include <vector>

namespace libcore::core {

struct LogEntry {
    long long   id = 0;
    long long   ts = 0;
    std::string level;    // info / warn / error
    std::string event;    // user.login, book.added, ...
    std::optional<long long>   actor_id;
    std::optional<std::string> payload;   // JSON-строка
};

// Записать событие.
void log_event(db::Database& db,
               const std::string& level,
               const std::string& event,
               std::optional<long long> actor_id = std::nullopt,
               std::optional<std::string> payload = std::nullopt);

// Хелперы
void log_info (db::Database& db, const std::string& event,
               std::optional<long long> actor_id = std::nullopt,
               std::optional<std::string> payload = std::nullopt);
void log_warn (db::Database& db, const std::string& event,
               std::optional<long long> actor_id = std::nullopt,
               std::optional<std::string> payload = std::nullopt);
void log_error(db::Database& db, const std::string& event,
               std::optional<long long> actor_id = std::nullopt,
               std::optional<std::string> payload = std::nullopt);

// Получить логи с фильтрами.
std::vector<LogEntry> list_logs(db::Database& db,
                                std::optional<std::string> level,
                                std::optional<std::string> event,
                                std::optional<long long>   since_ts,
                                int tail);

LogEntry row_to_log(const db::Row& row);

} // namespace libcore::core
