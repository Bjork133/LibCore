#include "core/books.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace libcore::core {

using db::Cell;
using db::text_cell;
using db::int_cell;
using db::null_cell;

// --- ISBN ---

std::string normalize_isbn(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c >= '0' && c <= '9') out.push_back(c);
        else if (c == 'x' || c == 'X') out.push_back('X');
    }
    return out;
}

// --- row → struct ---

Book row_to_book(const db::Row& row) {
    Book b;
    b.id       = db::row_int(row, 0);
    b.title    = db::row_text(row, 1);
    b.author   = db::row_text(row, 2);
    if (!db::row_is_null(row, 3))
        b.year = static_cast<int>(db::row_int(row, 3));
    if (!db::row_is_null(row, 4))
        b.isbn = db::row_text(row, 4);
    b.added_at = db::row_int(row, 5);
    if (!db::row_is_null(row, 6))
        b.added_by = db::row_int(row, 6);
    return b;
}

Copy row_to_copy(const db::Row& row) {
    Copy c;
    c.id       = db::row_int(row, 0);
    c.book_id  = db::row_int(row, 1);
    c.status   = db::row_text(row, 2);
    c.added_at = db::row_int(row, 3);
    return c;
}

// --- поиск ---

std::optional<Book> find_by_title_author(db::Database& database,
                                         const std::string& title,
                                         const std::string& author) {
    auto r = database.query_params(
        "SELECT id, title, author, year, isbn, added_at, added_by "
        "FROM books WHERE title = ? AND author = ? LIMIT 1;",
        { text_cell(title), text_cell(author) }
    );
    if (r.empty()) return std::nullopt;
    return row_to_book(r[0]);
}

std::optional<Book> find_by_id(db::Database& database, long long id) {
    auto r = database.query_params(
        "SELECT id, title, author, year, isbn, added_at, added_by "
        "FROM books WHERE id = ? LIMIT 1;",
        { int_cell(id) }
    );
    if (r.empty()) return std::nullopt;
    return row_to_book(r[0]);
}

// --- добавление ---

AddBookResult add_book(db::Database& database,
                       const std::string& title,
                       const std::string& author,
                       std::optional<int> year,
                       std::optional<std::string> isbn,
                       std::optional<long long> added_by) {
    AddBookResult res;

    // upsert: ищем существующую по title+author
    auto existing = find_by_title_author(database, title, author);
    if (existing) {
        res.book_id = existing->id;
        res.created = false;
        return res;
    }

    // создаём
    auto now = static_cast<long long>(std::time(nullptr));

    std::vector<Cell> params;
    params.push_back(text_cell(title));
    params.push_back(text_cell(author));
    params.push_back(year ? int_cell(*year) : null_cell());
    params.push_back(isbn && !isbn->empty() ? text_cell(*isbn) : null_cell());
    params.push_back(int_cell(now));
    params.push_back(added_by ? int_cell(*added_by) : null_cell());

    database.exec_params(
        "INSERT INTO books (title, author, year, isbn, added_at, added_by) "
        "VALUES (?, ?, ?, ?, ?, ?);",
        params
    );

    res.book_id = database.last_insert_rowid();
    res.created = true;
    return res;
}

// --- поиск ---

std::vector<Book> search_books(db::Database& database,
                               const std::string& query,
                               std::size_t limit) {
    std::vector<Book> out;

    if (query.empty()) {
        auto r = database.query_params(
            "SELECT id, title, author, year, isbn, added_at, added_by "
            "FROM books ORDER BY title LIMIT ?;",
            { int_cell(static_cast<long long>(limit)) }
        );
        for (const auto& row : r) out.push_back(row_to_book(row));
        return out;
    }

    std::string like = "%" + query + "%";
    auto r = database.query_params(
        "SELECT id, title, author, year, isbn, added_at, added_by "
        "FROM books "
        "WHERE title LIKE ? OR author LIKE ? OR isbn LIKE ? "
        "ORDER BY title LIMIT ?;",
        { text_cell(like), text_cell(like), text_cell(like),
          int_cell(static_cast<long long>(limit)) }
    );
    for (const auto& row : r) out.push_back(row_to_book(row));
    return out;
}

std::vector<Book> list_books(db::Database& database, std::size_t limit) {
    return search_books(database, "", limit);
}

// --- копии ---

std::vector<long long> add_copies(db::Database& database,
                                  long long book_id,
                                  int count) {
    std::vector<long long> ids;
    auto now = static_cast<long long>(std::time(nullptr));

    database.begin();
    try {
        for (int i = 0; i < count; ++i) {
            database.exec_params(
                "INSERT INTO copies (book_id, status, added_at) "
                "VALUES (?, 'available', ?);",
                { int_cell(book_id), int_cell(now) }
            );
            ids.push_back(database.last_insert_rowid());
        }
        database.commit();
    } catch (...) {
        database.rollback();
        throw;
    }
    return ids;
}

std::vector<Copy> list_copies(db::Database& database, long long book_id) {
    std::vector<Copy> out;
    auto r = database.query_params(
        "SELECT id, book_id, status, added_at "
        "FROM copies WHERE book_id = ? ORDER BY id;",
        { int_cell(book_id) }
    );
    for (const auto& row : r) out.push_back(row_to_copy(row));
    return out;
}

int count_available(db::Database& database, long long book_id) {
    auto r = database.query_params(
        "SELECT COUNT(*) FROM copies "
        "WHERE book_id = ? AND status = 'available';",
        { int_cell(book_id) }
    );
    if (r.empty() || r[0].empty()) return 0;
    return static_cast<int>(db::row_int(r[0], 0));
}

} // namespace libcore::core
