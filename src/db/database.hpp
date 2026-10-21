#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace libcore::db {

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& msg) : std::runtime_error(msg) {}
};

// Явный тип ячейки.
enum class CellType { Null, Int, Double, Text };

struct Cell {
    CellType type = CellType::Null;
    long long i = 0;
    double    d = 0.0;
    std::string s;

    bool is_null()   const { return type == CellType::Null; }
    bool is_int()    const { return type == CellType::Int; }
    bool is_double() const { return type == CellType::Double; }
    bool is_text()   const { return type == CellType::Text; }
};

using Row    = std::vector<Cell>;
using Result = std::vector<Row>;

// --- Конструкторы ячеек ---
inline Cell null_cell()                { return Cell{}; }
inline Cell int_cell(long long v)      { Cell c; c.type = CellType::Int;    c.i = v; return c; }
inline Cell double_cell(double v)      { Cell c; c.type = CellType::Double; c.d = v; return c; }
inline Cell text_cell(std::string v)   { Cell c; c.type = CellType::Text;   c.s = std::move(v); return c; }

// --- Чтение из Row ---
std::string row_text  (const Row& r, std::size_t idx);
long long   row_int   (const Row& r, std::size_t idx);
double      row_double(const Row& r, std::size_t idx);
bool        row_is_null(const Row& r, std::size_t idx);

class Database {
public:
    explicit Database(const std::filesystem::path& path);
    static Database open_existing(const std::filesystem::path& path);

    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    void exec(std::string_view sql);
    Result query(std::string_view sql);

    void exec_params(std::string_view sql, const std::vector<Cell>& params);
    Result query_params(std::string_view sql, const std::vector<Cell>& params);

    void begin();
    void commit();
    void rollback();

    long long last_insert_rowid();

    sqlite3* raw() { return db_; }

private:
    sqlite3* db_ = nullptr;

    void check(int rc, std::string_view context);
    void bind_params(sqlite3_stmt* stmt, const std::vector<Cell>& params);
    Result run_query(sqlite3_stmt* stmt);
};

} // namespace libcore::db
