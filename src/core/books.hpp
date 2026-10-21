#pragma once

#include "db/database.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace libcore::core {

struct Book {
    long long   id = 0;
    std::string title;
    std::string author;
    std::optional<int>    year;
    std::optional<std::string> isbn;
    long long   added_at = 0;
    std::optional<long long> added_by;
};

struct Copy {
    long long   id = 0;
    long long   book_id = 0;
    std::string status;         // available / loaned / lost / retired
    long long   added_at = 0;
};

// --- ISBN ---
// Нормализует ISBN: убирает дефисы и пробелы, оставляет цифры и X.
// Возвращает пустую строку, если на входе пусто.
std::string normalize_isbn(const std::string& raw);

// --- Книги ---

// Ищет книгу по title+author (точное совпадение, регистронезависимо).
std::optional<Book> find_by_title_author(db::Database& db,
                                         const std::string& title,
                                         const std::string& author);

// Ищет книгу по ID.
std::optional<Book> find_by_id(db::Database& db, long long id);

// Добавляет книгу. Если книга с таким title+author уже есть —
// возвращает её ID (upsert). Иначе создаёт новую.
// Возвращает {book_id, created}.
struct AddBookResult {
    long long book_id = 0;
    bool created = false;
};
AddBookResult add_book(db::Database& db,
                       const std::string& title,
                       const std::string& author,
                       std::optional<int> year,
                       std::optional<std::string> isbn,
                       std::optional<long long> added_by);

// Поиск книг по подстроке в title, author или ISBN.
// Если query пустой — возвращает все.
std::vector<Book> search_books(db::Database& db,
                               const std::string& query,
                               std::size_t limit = 100);

// Все книги (сокращение для search_books("")).
std::vector<Book> list_books(db::Database& db, std::size_t limit = 100);

// --- Копии ---

// Создаёт N копий для книги, возвращает их ID.
std::vector<long long> add_copies(db::Database& db,
                                  long long book_id,
                                  int count);

// Все копии книги.
std::vector<Copy> list_copies(db::Database& db, long long book_id);

// Сколько копий в статусе available.
int count_available(db::Database& db, long long book_id);

// --- Хелперы ---

// Преобразование Row → Book.
Book row_to_book(const db::Row& row);

// Преобразование Row → Copy.
Copy row_to_copy(const db::Row& row);

} // namespace libcore::core
