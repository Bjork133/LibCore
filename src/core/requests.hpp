#pragma once

#include "db/database.hpp"

#include <optional>
#include <string>
#include <vector>

namespace libcore::core {

struct Request {
    long long id = 0;
    long long book_id = 0;
    long long user_id = 0;
    long long created_at = 0;
    std::string status;  // pending / approved / rejected
};

// Создать запрос (reader). Возвращает ID.
long long create_request(db::Database& db, long long book_id, long long user_id);

// Список запросов по фильтрам (moderator+).
std::vector<Request> list_requests(db::Database& db,
                                   std::optional<long long> book_id,
                                   std::optional<long long> user_id,
                                   std::optional<std::string> status);

// Отклонить запрос.
bool reject_request(db::Database& db, long long request_id);

Request row_to_request(const db::Row& row);

} // namespace libcore::core
