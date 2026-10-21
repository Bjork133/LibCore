#include "util/dateparse.hpp"

#include <cctype>
#include <cstring>
#include <ctime>
#include <sstream>

namespace libcore::util {

namespace {

// Возвращает длину подряд идущих одинаковых символов.
std::size_t run_length(const std::string& s, std::size_t start, char c) {
    std::size_t i = start;
    while (i < s.size() && s[i] == c) ++i;
    return i - start;
}

} // namespace

bool is_valid_pattern(const std::string& pattern) {
    bool has_y = false, has_m = false, has_d = false;
    for (char c : pattern) {
        if (c == 'Y') has_y = true;
        else if (c == 'M') has_m = true;
        else if (c == 'D') has_d = true;
    }
    return has_y && has_m && has_d;
}

std::optional<long long> parse_date(const std::string& input,
                                    const std::string& pattern) {
    if (!is_valid_pattern(pattern)) return std::nullopt;

    int year = 0, month = 0, day = 0;
    std::size_t ip = 0;  // позиция в input
    std::size_t pp = 0;  // позиция в pattern

    while (pp < pattern.size()) {
        char pc = pattern[pp];

        if (pc == 'Y' || pc == 'M' || pc == 'D') {
            std::size_t run = run_length(pattern, pp, pc);

            // Сколько цифр ожидаем
            std::size_t digits;
            if (pc == 'Y') digits = run;         // ровно run цифр
            else           digits = 2;            // M и D — всегда 2

            // Собираем цифры из input
            std::string num;
            while (num.size() < digits && ip < input.size()
                   && std::isdigit(static_cast<unsigned char>(input[ip]))) {
                num.push_back(input[ip++]);
            }
            if (num.empty()) return std::nullopt;

            int v = 0;
            try { v = std::stoi(num); } catch (...) { return std::nullopt; }

            if (pc == 'Y') year = v;
            else if (pc == 'M') month = v;
            else day = v;

            pp += run;
        } else {
            // разделитель — должен совпасть с input
            if (ip >= input.size() || input[ip] != pc) return std::nullopt;
            ++ip;
            ++pp;
        }
    }

    // В input не должно остаться лишнего
    if (ip != input.size()) return std::nullopt;

    // Проверим диапазоны
    if (month < 1 || month > 12) return std::nullopt;
    if (day < 1 || day > 31) return std::nullopt;

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;

    std::time_t t = timegm(&tm);
    if (t == -1) return std::nullopt;
    return static_cast<long long>(t);
}

std::string format_date(long long unix_time, const std::string& pattern) {
    std::time_t t = static_cast<std::time_t>(unix_time);
    std::tm tm{};
    gmtime_r(&t, &tm);

    std::string out;
    out.reserve(pattern.size() + 4);

    std::size_t pp = 0;
    while (pp < pattern.size()) {
        char pc = pattern[pp];

        if (pc == 'Y') {
            std::size_t run = run_length(pattern, pp, pc);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%0*d", static_cast<int>(run), tm.tm_year + 1900);
            out += buf;
            pp += run;
        } else if (pc == 'M') {
            std::size_t run = run_length(pattern, pp, pc);
            (void)run;
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02d", tm.tm_mon + 1);
            out += buf;
            pp += 2;
        } else if (pc == 'D') {
            std::size_t run = run_length(pattern, pp, pc);
            (void)run;
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02d", tm.tm_mday);
            out += buf;
            pp += 2;
        } else {
            out += pc;
            ++pp;
        }
    }
    return out;
}

} // namespace libcore::util
