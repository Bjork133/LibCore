#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace libcore::help {

// Копирует вшитые хелпы в ~/.config/libcore/help/, если их там нет.
void install_embedded_if_missing();

// Склеивает аргументы (book, add) → "bookadd", ищет файл:
//   1) ~/.config/libcore/help/<name>.txt
//   2) вшитый fallback
// Возвращает содержимое или nullopt.
std::optional<std::string> find(std::string_view name);

// Для диагностики: путь, где искали.
std::string search_path_for(std::string_view name);

} // namespace libcore::help
