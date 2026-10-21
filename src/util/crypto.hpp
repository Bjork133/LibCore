#pragma once

#include <string>
#include <string_view>

namespace libcore::util {

// Хэширует пароль. Возвращает строку формата:
//   scrypt$N$r$p$salt_b64$hash_b64
std::string hash_password(std::string_view plain);

// Проверяет пароль против строки из hash_password().
bool verify_password(std::string_view plain, std::string_view stored);

// --- Base64 (стандартный, с padding) ---
std::string base64_encode(const unsigned char* data, std::size_t len);
std::string base64_encode(std::string_view s);
bool        base64_decode(std::string_view in, std::string& out);

} // namespace libcore::util
