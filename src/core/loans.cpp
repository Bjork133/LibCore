#include "core/loans.hpp"

#include "core/settings.hpp"

#include <ctime>
#include <stdexcept>

namespace libcore::core {

using db::Cell;
using db::text_cell;
using db::int_cell;
using db::null_cell;

Loan row_to_loan(const db::Row& row) {
    Loan l;
    l.id       = db::row_int(row, 0);
    l.copy_id  = db::row_int(row, 1);
    l.book_id  = db::row_int(row, 2);
    l.user_id  = db::row_int(row, 3);
    l.givedate = db::row_int(row, 4);
    if (!db::row_is_null(row, 5)) l.enddate    = db::row_int(row, 5);
    if (!db::row_is_null(row, 6)) l.returndate = db::row_int(row, 6);
    return l;
}

std::optional<long long> find_available_copy(db::Database& database, long long book_id) {
    auto r = database.query_params(
        "SELECT id FROM copies "
        "WHERE book_id = ? AND status = 'available' "
        "ORDER BY id LIMIT 1;",
        { int_cell(book_id) }
    );
    if (r.empty()) return std::nullopt;
    return db::row_int(r[0], 0);
}

long long grant_loan(db::Database& database,
                     long long book_id,
                     long long user_id,
                     std::optional<long long> until_end) {
    auto copy_opt = find_available_copy(database, book_id);
    if (!copy_opt) {
        throw std::runtime_error("no available copies for book " + std::to_string(book_id));
    }
    long long copy_id = *copy_opt;

    auto now = static_cast<long long>(std::time(nullptr));

    // Если until_end не задан — считаем по loan_days.
    std::optional<long long> enddate = until_end;
    if (!enddate) {
        auto s = load_settings();
        if (s.loan_days > 0) {
            enddate = now + static_cast<long long>(s.loan_days) * 24 * 3600;
        }
        // если loan_days == 0 — enddate остаётся nullopt (бессрочно)
    }

    database.begin();
    try {
        // 1. loan
        database.exec_params(
            "INSERT INTO loans (copy_id, book_id, user_id, givedate, enddate) "
            "VALUES (?, ?, ?, ?, ?);",
            {
                int_cell(copy_id),
                int_cell(book_id),
                int_cell(user_id),
                int_cell(now),
                enddate ? int_cell(*enddate) : null_cell()
            }
        );
        long long loan_id = database.last_insert_rowid();

        // 2. статус копии
        database.exec_params(
            "UPDATE copies SET status = 'loaned' WHERE id = ?;",
            { int_cell(copy_id) }
        );

        // 3. pending-запрос этого юзера на эту книгу → approved
        database.exec_params(
            "UPDATE requests SET status = 'approved' "
            "WHERE book_id = ? AND user_id = ? AND status = 'pending';",
            { int_cell(book_id), int_cell(user_id) }
        );

        database.commit();
        return loan_id;
    } catch (...) {
        database.rollback();
        throw;
    }
}

bool return_loan(db::Database& database, long long copy_id) {
    // Ищем активную выдачу
    auto r = database.query_params(
        "SELECT id, book_id FROM loans "
        "WHERE copy_id = ? AND returndate IS NULL "
        "ORDER BY id DESC LIMIT 1;",
        { int_cell(copy_id) }
    );
    if (r.empty()) return false;

    long long loan_id = db::row_int(r[0], 0);
    auto now = static_cast<long long>(std::time(nullptr));

    database.begin();
    try {
        database.exec_params(
            "UPDATE loans SET returndate = ? WHERE id = ?;",
            { int_cell(now), int_cell(loan_id) }
        );
        database.exec_params(
            "UPDATE copies SET status = 'available' WHERE id = ?;",
            { int_cell(copy_id) }
        );
        database.commit();
        return true;
    } catch (...) {
        database.rollback();
        throw;
    }
}

std::vector<Loan> active_loans_for_book(db::Database& database, long long book_id) {
    std::vector<Loan> out;
    auto r = database.query_params(
        "SELECT id, copy_id, book_id, user_id, givedate, enddate, returndate "
        "FROM loans WHERE book_id = ? AND returndate IS NULL ORDER BY id;",
        { int_cell(book_id) }
    );
    for (const auto& row : r) out.push_back(row_to_loan(row));
    return out;
}

std::vector<Loan> active_loans_for_user(db::Database& database, long long user_id) {
    std::vector<Loan> out;
    auto r = database.query_params(
        "SELECT id, copy_id, book_id, user_id, givedate, enddate, returndate "
        "FROM loans WHERE user_id = ? AND returndate IS NULL ORDER BY id;",
        { int_cell(user_id) }
    );
    for (const auto& row : r) out.push_back(row_to_loan(row));
    return out;
}

std::optional<Loan> active_loan_for_copy(db::Database& database, long long copy_id) {
    auto r = database.query_params(
        "SELECT id, copy_id, book_id, user_id, givedate, enddate, returndate "
        "FROM loans WHERE copy_id = ? AND returndate IS NULL "
        "ORDER BY id DESC LIMIT 1;",
        { int_cell(copy_id) }
    );
    if (r.empty()) return std::nullopt;
    return row_to_loan(r[0]);
}

} // namespace libcore::core
