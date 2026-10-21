#pragma once

#include "db/database.hpp"

#include <optional>
#include <string>
#include <vector>

namespace libcore::core {

struct Loan {
    long long id = 0;
    long long copy_id = 0;
    long long book_id = 0;
    long long user_id = 0;
    long long givedate = 0;
    std::optional<long long> enddate;
    std::optional<long long> returndate;
};

// --- Выдача ---

// Находит available-копию для книги (любую).
std::optional<long long> find_available_copy(db::Database& db, long long book_id);

// Выдаёт книгу. Если until_end == nullopt — считает по loan_days.
// Возвращает ID созданной записи loans.
// Бросает runtime_error, если нет available-копии.
long long grant_loan(db::Database& db,
                     long long book_id,
                     long long user_id,
                     std::optional<long long> until_end);

// Возврат: помечает returndate = now, копия → available.
// Возвращает false, если копия не выдана.
bool return_loan(db::Database& db, long long copy_id);

// --- Активные выдачи ---

// Все выданные копии книги (returndate IS NULL).
std::vector<Loan> active_loans_for_book(db::Database& db, long long book_id);

// Все выдачи пользователя (активные).
std::vector<Loan> active_loans_for_user(db::Database& db, long long user_id);

// Активная выдача по копии (returndate IS NULL), если есть.
std::optional<Loan> active_loan_for_copy(db::Database& db, long long copy_id);

// --- Утилиты ---

Loan row_to_loan(const db::Row& row);

} // namespace libcore::core
