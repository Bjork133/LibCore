#pragma once

#include <ctime>
#include <optional>
#include <string>

namespace libcore::util {

// Парсит дату по макету (например "DD.MM.YYYY").
// Возвращает unix time (полночь UTC) или nullopt.
// Макет: Y = год, M = месяц, D = день. Всё остальное — разделители.
// Год дополняется нулями слева до длины макета Y.
// Месяц/день — до 2 знаков.
std::optional<long long> parse_date(const std::string& input,
                                    const std::string& pattern);

// Форматирует unix time в строку по макету.
std::string format_date(long long unix_time, const std::string& pattern);

// Проверка, что в макете есть Y, M, D.
bool is_valid_pattern(const std::string& pattern);

} // namespace libcore::util
