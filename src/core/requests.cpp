#include "core/requests.hpp"

#include <ctime>
#include <stdexcept>

namespace libcore::core {

using db::text_cell;
using db::int_cell;

Request row_to_request(const db::Row& row) {
    Request r;
    r.id         = db::row_int(row, 0);
    r.book_id    = db::row_int(row, 1);
    r.user_id    = db::row_int(row, 2);
    r.created_at = db::row_int(row, 3);
    r.status     = db::row_text(row, 4);
    return r;
}

long long create_request(db::Database& database, long long book_id, long long user_id) {
    // Проверим, нет ли уже pending-запроса от этого юзера на эту книгу.
    auto existing = database.query_params(
        "SELECT id FROM requests "
        "WHERE book_id = ? AND user_id = ? AND status = 'pending' "
        "LIMIT 1;",
        { int_cell(book_id), int_cell(user_id) }
    );
    if (!existing.empty()) {
        throw std::runtime_error("request already pending");
    }

    auto now = static_cast<long long>(std::time(nullptr));

    database.exec_params(
        "INSERT INTO requests (book_id, user_id, created_at, status) "
        "VALUES (?, ?, ?, 'pending');",
        { int_cell(book_id), int_cell(user_id), int_cell(now) }
    );
    return database.last_insert_rowid();
}

std::vector<Request> list_requests(db::Database& database,
                                   std::optional<long long> book_id,
                                   std::optional<long long> user_id,
                                   std::optional<std::string> status) {
    std::string sql = "SELECT id, book_id, user_id, created_at, status FROM requests WHERE 1=1";
    std::vector<db::Cell> params;

    if (book_id) { sql += " AND book_id = ?"; params.push_back(int_cell(*book_id)); }
    if (user_id) { sql += " AND user_id = ?"; params.push_back(int_cell(*user_id)); }
    if (status)  { sql += " AND status = ?";  params.push_back(text_cell(*status)); }

    sql += " ORDER BY created_at;";

    std::vector<Request> out;
    auto r = database.query_params(sql, params);
    for (const auto& row : r) out.push_back(row_to_request(row));
    return out;
}

bool reject_request(db::Database& database, long long request_id) {
    auto r = database.query_params(
        "SELECT status FROM requests WHERE id = ? LIMIT 1;",
        { int_cell(request_id) }
    );
    if (r.empty()) return false;
    if (db::row_text(r[0], 0) != "pending") return false;

    database.exec_params(
        "UPDATE requests SET status = 'rejected' WHERE id = ?;",
        { int_cell(request_id) }
    );
    return true;
}

} // namespace libcore::core
