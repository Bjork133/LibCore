#include "db/migrations.hpp"

#include <ctime>
#include <format>

namespace libcore::db {

static std::vector<Migration> build_migrations() {
    std::vector<Migration> m;

    // --- 001: ядро системы ---
    m.push_back({
        1,
        "core: meta, users, sessions",
        R"SQL(
            CREATE TABLE meta (
                key   TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );

            CREATE TABLE users (
                id             INTEGER PRIMARY KEY AUTOINCREMENT,
                username       TEXT NOT NULL UNIQUE,
                pw_hash        TEXT NOT NULL,
                role           TEXT NOT NULL CHECK (role IN ('host','moderator','reader')),
                created_at     INTEGER NOT NULL,
                last_seen_at   INTEGER,
                profile_public INTEGER NOT NULL DEFAULT 0,
                books_public   INTEGER NOT NULL DEFAULT 0,
                stats_public   INTEGER NOT NULL DEFAULT 0
            );

            CREATE TABLE sessions (
                token       TEXT PRIMARY KEY,
                user_id     INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                created_at  INTEGER NOT NULL,
                expires_at  INTEGER NOT NULL
            );

            CREATE INDEX idx_sessions_user ON sessions(user_id);
            CREATE INDEX idx_sessions_exp  ON sessions(expires_at);
        )SQL"
    });

    // --- 002: книги и копии ---
    m.push_back({
        2,
        "books and copies",
        R"SQL(
            CREATE TABLE books (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                title      TEXT NOT NULL,
                author     TEXT NOT NULL,
                year       INTEGER,
                isbn       TEXT,
                added_at   INTEGER NOT NULL,
                added_by   INTEGER REFERENCES users(id) ON DELETE SET NULL
            );

            CREATE INDEX idx_books_title  ON books(title);
            CREATE INDEX idx_books_author ON books(author);
            CREATE INDEX idx_books_isbn   ON books(isbn);

            CREATE TABLE copies (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                book_id    INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
                status     TEXT NOT NULL DEFAULT 'available'
                           CHECK (status IN ('available','loaned','lost','retired')),
                added_at   INTEGER NOT NULL
            );

            CREATE INDEX idx_copies_book   ON copies(book_id);
            CREATE INDEX idx_copies_status ON copies(status);
        )SQL"
    });

    // --- 003: loans и requests ---
    m.push_back({
        3,
        "loans and requests",
        R"SQL(
            CREATE TABLE loans (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                copy_id    INTEGER NOT NULL REFERENCES copies(id) ON DELETE CASCADE,
                book_id    INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
                user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                givedate   INTEGER NOT NULL,
                enddate    INTEGER,
                returndate INTEGER
            );

            CREATE INDEX idx_loans_copy   ON loans(copy_id);
            CREATE INDEX idx_loans_user   ON loans(user_id);
            CREATE INDEX idx_loans_book   ON loans(book_id);
            CREATE INDEX idx_loans_active ON loans(returndate) WHERE returndate IS NULL;

            CREATE TABLE requests (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                book_id    INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
                user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                created_at INTEGER NOT NULL,
                status     TEXT NOT NULL DEFAULT 'pending'
                           CHECK (status IN ('pending','approved','rejected'))
            );

            CREATE INDEX idx_requests_book   ON requests(book_id);
            CREATE INDEX idx_requests_user   ON requests(user_id);
            CREATE INDEX idx_requests_status ON requests(status);
        )SQL"
    });

    // --- 004: logs ---
    m.push_back({
        4,
        "logs",
        R"SQL(
            CREATE TABLE logs (
                id         INTEGER PRIMARY KEY AUTOINCREMENT,
                ts         INTEGER NOT NULL,
                level      TEXT NOT NULL CHECK (level IN ('info','warn','error')),
                event      TEXT NOT NULL,
                actor_id   INTEGER REFERENCES users(id) ON DELETE SET NULL,
                payload    TEXT
            );

            CREATE INDEX idx_logs_ts    ON logs(ts);
            CREATE INDEX idx_logs_level ON logs(level);
            CREATE INDEX idx_logs_event ON logs(event);
        )SQL"
    });

    return m;
}

const std::vector<Migration>& all_migrations() {
    static const std::vector<Migration> migrations = build_migrations();
    return migrations;
}

int current_schema_version(Database& db) {
    auto r = db.query(
        "SELECT name FROM sqlite_master "
        "WHERE type='table' AND name='schema_version';"
    );
    if (r.empty()) return 0;

    auto v = db.query("SELECT MAX(version) FROM schema_version;");
    if (v.empty() || v[0].empty() || v[0][0].is_null()) return 0;
    return static_cast<int>(row_int(v[0], 0));
}

int apply_migrations(Database& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "  version     INTEGER PRIMARY KEY,"
        "  applied_at  INTEGER NOT NULL,"
        "  description TEXT NOT NULL"
        ");"
    );

    int current = current_schema_version(db);
    int applied = current;

    for (const auto& mig : all_migrations()) {
        if (mig.version <= current) continue;

        db.begin();
        try {
            db.exec(mig.sql);
            db.exec_params(
                "INSERT INTO schema_version (version, applied_at, description) "
                "VALUES (?, ?, ?);",
                {
                    int_cell(mig.version),
                    int_cell(static_cast<long long>(std::time(nullptr))),
                    text_cell(mig.description)
                }
            );
            db.commit();
            applied = mig.version;
        } catch (...) {
            db.rollback();
            throw;
        }
    }

    return applied;
}

} // namespace libcore::db
