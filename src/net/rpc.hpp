#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace libcore::net {

using json = nlohmann::json;

// Обработать один RPC-запрос.
//   library — имя библиотеки (для открытия БД)
//   request — JSON вида { "cmd": "...", "args": {...}, "token": "..." }
//
// Возвращает JSON-ответ: { "ok": true, "data": ... } или
//                         { "ok": false, "error": { "code": "...", "message": "..." } }
json dispatch_rpc(const std::string& library, const json& request);

} // namespace libcore::net
