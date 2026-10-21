#include "core/server_meta.hpp"
#include "util/crypto.hpp"
#include "db/database.hpp"
#include "db/migrations.hpp"
#include "util/file.hpp"
#include "util/paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <format>
#include <random>
#include <stdexcept>

namespace libcore::core {

namespace fs = std::filesystem;
using json = nlohmann::json;

// --- Пути ---

fs::path libraries_root() {
    return paths::data_dir() / "libraries";
}

fs::path library_dir(const std::string& name) {
    return libraries_root() / name;
}

fs::path library_db_path(const std::string& name) {
    return library_dir(name) / "library.db";
}

fs::path library_meta_path(const std::string& name) {
    return library_dir(name) / "server.json";
}

// --- Список библиотек ---

std::vector<std::string> list_libraries() {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(libraries_root(), ec)) return out;

    for (const auto& entry : fs::directory_iterator(libraries_root(), ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        // библиотека валидна, если есть library.db
        if (fs::exists(entry.path() / "library.db")) {
            out.push_back(entry.path().filename().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool library_exists(const std::string& name) {
    return fs::exists(library_db_path(name));
}

// --- server.json ---

std::optional<ServerMeta> read_meta(const std::string& name) {
    auto path = library_meta_path(name);
    auto content = util::read_file(path);
    if (!content) return std::nullopt;

    try {
        auto j = json::parse(*content);
        ServerMeta m;
        m.name       = j.value("name", name);
        m.host       = j.value("host", "");
        m.port       = j.value("port", 8420);
        m.code       = j.value("code", "");
        m.created_at = j.value("created_at", 0LL);
        return m;
    } catch (...) {
        return std::nullopt;
    }
}

void write_meta(const ServerMeta& meta) {
    json j;
    j["name"]       = meta.name;
    j["host"]       = meta.host;
    j["port"]       = meta.port;
    j["code"]       = meta.code;
    j["created_at"] = meta.created_at;

    auto path = library_meta_path(meta.name);
    if (!util::write_file(path, j.dump(2) + "\n")) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

// --- Генерация кода ---

std::string generate_code() {
    // 8 цифр, криптостойкий источник.
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<int> dist(0, 9);

    std::string s;
    s.reserve(8);
    for (int i = 0; i < 8; ++i) {
        s.push_back(static_cast<char>('0' + dist(gen)));
    }
    return s;
}

// --- create_library ---

ServerMeta create_library(const std::string& name,
                          const std::string& host,
                          const std::string& password,
                          int port) {
    if (name.empty()) throw std::runtime_error("library name is empty");
    if (host.empty()) throw std::runtime_error("host username is empty");
    if (password.empty()) throw std::runtime_error("password is empty");
    if (library_exists(name)) {
        throw std::runtime_error("library already exists: " + name);
    }

    std::error_code ec;
    fs::create_directories(library_dir(name), ec);
    if (ec) throw std::runtime_error("cannot create " + library_dir(name).string());

    db::Database database(library_db_path(name));
    db::apply_migrations(database);

    // Хэш пароля host'а
    std::string pw_hash = util::hash_password(password);

    auto now = static_cast<long long>(std::time(nullptr));

    database.exec_params(
        "INSERT INTO users (username, pw_hash, role, created_at) "
        "VALUES (?, ?, 'host', ?);",
        {
            db::text_cell(host),
            db::text_cell(pw_hash),
            db::int_cell(now)
        }
    );

    auto put_meta = [&](const std::string& k, const std::string& v) {
        database.exec_params(
            "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?);",
            { db::text_cell(k), db::text_cell(v) }
        );
    };
    put_meta("library_name", name);
    put_meta("host_username", host);
    put_meta("created_at", std::to_string(now));

    ServerMeta meta;
    meta.name       = name;
    meta.host       = host;
    meta.port       = port;
    meta.code       = generate_code();
    meta.created_at = now;

    write_meta(meta);
    return meta;
}

} // namespace libcore::core
