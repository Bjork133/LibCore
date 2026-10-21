#pragma once

#include "db/database.hpp"

#include <string>
#include <vector>

namespace libcore::db {

// Одна миграция: версия, описание, SQL.
struct Migration {
    int version;
    std::string description;
    std::string sql;
};

// Список всех миграций в порядке применения.
const std::vector<Migration>& all_migrations();

// Применяет недостающие миграции. Возвращает новую версию схемы.
// Бросает Error при любой ошибке (транзакция откатится).
int apply_migrations(Database& db);

// Текущая версия схемы (0, если таблицы schema_version ещё нет).
int current_schema_version(Database& db);

} // namespace libcore::db
