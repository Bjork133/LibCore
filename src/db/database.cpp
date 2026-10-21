#include "db/database.hpp"

#include <sqlite3.h>

#include <utility>

namespace libcore::db {

std::string row_text(const Row& r, std::size_t idx) {
    if (idx >= r.size()) return {};
    return r[idx].s;
}

long long row_int(const Row& r, std::size_t idx) {
    if (idx >= r.size()) return 0;
    return r[idx].i;
}

double row_double(const Row& r, std::size_t idx) {
    if (idx >= r.size()) return 0.0;
    return r[idx].d;
}

bool row_is_null(const Row& r, std::size_t idx) {
    if (idx >= r.size()) return true;
    return r[idx].is_null();
}

// --- Database ---

Database::Database(const std::filesystem::path& path) {
    int rc = sqlite3_open(path.string().c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown";
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        throw Error("cannot open database: " + msg);
    }
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;",   nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA journal_mode = WAL;",  nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous = NORMAL;",nullptr, nullptr, nullptr);
}

Database Database::open_existing(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        throw Error("database does not exist: " + path.string());
    }
    return Database(path);
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

Database::Database(Database&& other) noexcept : db_(other.db_) {
    other.db_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

void Database::check(int rc, std::string_view context) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "unknown";
        throw Error(std::string(context) + ": " + msg);
    }
}

void Database::exec(std::string_view sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        if (err) sqlite3_free(err);
        throw Error("exec failed: " + msg);
    }
}

Result Database::query(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw Error(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    auto result = run_query(stmt);
    sqlite3_finalize(stmt);
    return result;
}

void Database::bind_params(sqlite3_stmt* stmt, const std::vector<Cell>& params) {
    for (std::size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        int idx = static_cast<int>(i + 1);
        int rc = SQLITE_OK;
        switch (p.type) {
            case CellType::Null:
                rc = sqlite3_bind_null(stmt, idx);
                break;
            case CellType::Int:
                rc = sqlite3_bind_int64(stmt, idx, p.i);
                break;
            case CellType::Double:
                rc = sqlite3_bind_double(stmt, idx, p.d);
                break;
            case CellType::Text:
                rc = sqlite3_bind_text(stmt, idx, p.s.c_str(),
                                       static_cast<int>(p.s.size()),
                                       SQLITE_TRANSIENT);
                break;
        }
        check(rc, "bind");
    }
}

Result Database::run_query(sqlite3_stmt* stmt) {
    Result result;
    int ncols = sqlite3_column_count(stmt);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Row row;
        row.reserve(ncols);
        for (int i = 0; i < ncols; ++i) {
            Cell c;
            switch (sqlite3_column_type(stmt, i)) {
                case SQLITE_NULL:
                    c.type = CellType::Null;
                    break;
                case SQLITE_INTEGER:
                    c.type = CellType::Int;
                    c.i = sqlite3_column_int64(stmt, i);
                    break;
                case SQLITE_FLOAT:
                    c.type = CellType::Double;
                    c.d = sqlite3_column_double(stmt, i);
                    break;
                case SQLITE_TEXT: {
                    c.type = CellType::Text;
                    const unsigned char* txt = sqlite3_column_text(stmt, i);
                    int len = sqlite3_column_bytes(stmt, i);
                    c.s.assign(reinterpret_cast<const char*>(txt), len);
                    break;
                }
                case SQLITE_BLOB: {
                    c.type = CellType::Text;
                    const void* blob = sqlite3_column_blob(stmt, i);
                    int len = sqlite3_column_bytes(stmt, i);
                    c.s.assign(reinterpret_cast<const char*>(blob), len);
                    break;
                }
            }
            row.push_back(std::move(c));
        }
        result.push_back(std::move(row));
    }
    if (rc != SQLITE_DONE) {
        throw Error(std::string("step failed: ") + sqlite3_errmsg(db_));
    }
    return result;
}

void Database::exec_params(std::string_view sql, const std::vector<Cell>& params) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw Error(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    bind_params(stmt, params);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw Error(std::string("exec_params failed: ") + sqlite3_errmsg(db_));
    }
}

Result Database::query_params(std::string_view sql, const std::vector<Cell>& params) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw Error(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    bind_params(stmt, params);
    auto result = run_query(stmt);
    sqlite3_finalize(stmt);
    return result;
}

void Database::begin()    { exec("BEGIN;"); }
void Database::commit()   { exec("COMMIT;"); }
void Database::rollback() { exec("ROLLBACK;"); }

long long Database::last_insert_rowid() {
    return sqlite3_last_insert_rowid(db_);
}

} // namespace libcore::db
