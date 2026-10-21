# Версии компонентов LibCore

## Язык и сборка
- C++20
- CMake >= 3.20
- GCC >= 16 (проверено на 16.2.1)

## Вендоренные библиотеки (third_party/)
| Компонент | Версия | Источник | Дата |
|---|---|---|---|
| SQLite | 3.50.4 | https://sqlite.org/ | 2026-10-11 |
| nlohmann/json | latest | https://github.com/nlohmann/json | 2026-10-11 |
| scrypt (enceeper) | 2018-12-06 | https://github.com/enceeper/scrypt | 2026-10-12 | GPL-3.0 |

## Схема БД
| Версия | Что добавлено | Дата |
|---|---|---|
| 001 | meta, users, sessions, schema_version | (заполним) |
| 002 | books, copies | (план) |
| 003 | loans, requests | (план) |
| 004 | logs | (план) |
