#include "core/library_db.hpp"

#include "core/server_meta.hpp"
#include "db/migrations.hpp"

namespace libcore::core {

db::Database open_library_db(const std::string& library_name) {
    db::Database database(library_db_path(library_name));
    db::apply_migrations(database);
    return database;
}

} // namespace libcore::core
