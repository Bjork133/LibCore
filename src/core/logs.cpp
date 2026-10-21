#include "core/logs.hpp"

#include <ctime>

namespace libcore::core {

using db::text_cell;
using db::int_cell;
using db::null_cell;

LogEntry row_to_log(const db::Row& row) {
    LogEntry l;
    l.id     = db::row_int(row, 0);
    l.ts     = db::row_int(row, 1);
    l.level  = db::row_text(row, 2);
    l.event  = db::row_text(row, 3);
    if (!db::row_is_null(row, 4)) l.actor_id = db::row_int(row, 4);
    if (!db::row_is_null(row, 5)) l.payload  = db::row_text(row, 5);
    return l;
}

void log_event(db::Database& database,
               const std::string& level,
               const std::string& event,
               std::optional<long long> actor_id,
               std::optional<std::string> payload) {
    auto now = static_cast<long long>(std::time(nullptr));
    database.exec_params(
        "INSERT INTO logs (ts, level, event, actor_id, payload) "
        "VALUES (?, ?, ?, ?, ?);",
        {
            int_cell(now),
            text_cell(level),
            text_cell(event),
            actor_id ? int_cell(*actor_id) : null_cell(),
            payload  ? text_cell(*payload) : null_cell()
        }
    );
}

void log_info(db::Database& database, const std::string& event,
              std::optional<long long> actor_id,
              std::optional<std::string> payload) {
    log_event(database, "info", event, actor_id, payload);
}

void log_warn(db::Database& database, const std::string& event,
              std::optional<long long> actor_id,
              std::optional<std::string> payload) {
    log_event(database, "warn", event, actor_id, payload);
}

void log_error(db::Database& database, const std::string& event,
               std::optional<long long> actor_id,
               std::optional<std::string> payload) {
    log_event(database, "error", event, actor_id, payload);
}

std::vector<LogEntry> list_logs(db::Database& database,
                                std::optional<std::string> level,
                                std::optional<std::string> event,
                                std::optional<long long>   since_ts,
                                int tail) {
    std::string sql =
        "SELECT id, ts, level, event, actor_id, payload FROM logs WHERE 1=1";
    std::vector<db::Cell> params;

    if (level) { sql += " AND level = ?"; params.push_back(text_cell(*level)); }
    if (event) { sql += " AND event = ?"; params.push_back(text_cell(*event)); }
    if (since_ts) { sql += " AND ts >= ?"; params.push_back(int_cell(*since_ts)); }

    sql += " ORDER BY id DESC LIMIT ?";
    params.push_back(int_cell(tail));

    std::vector<LogEntry> out;
    auto r = database.query_params(sql, params);
    out.reserve(r.size());
    // Разворачиваем — хотим хронологический порядок
    for (auto it = r.rbegin(); it != r.rend(); ++it) {
        out.push_back(row_to_log(*it));
    }
    return out;
}

} // namespace libcore::core
