#pragma once

#include "db/database.hpp"

#include <string>

namespace libcore::core {

db::Database open_library_db(const std::string& library_name);

} // namespace libcore::core
