#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace libcore::util {

// Читает файл целиком. nullopt — если не удалось.
std::optional<std::string> read_file(const std::filesystem::path& p);

// Пишет файл. true — успех.
bool write_file(const std::filesystem::path& p, std::string_view data);

// Существует ли файл (регулярный).
bool file_exists(const std::filesystem::path& p);

} // namespace libcore::util
