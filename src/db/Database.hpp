#pragma once
#include <sqlite3.h>

#include <string>

namespace todo {

// Dono: Codex. Wrapper RAII fino sobre sqlite3.
class Database {
public:
    // ":memory:" permitido (testes). Lanca std::runtime_error se falhar.
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* handle() const noexcept { return db_; }

    // Executa SQL sem resultado. Lanca std::runtime_error com sqlite3_errmsg.
    void exec(const std::string& sql);

    // PRAGMA foreign_keys=ON + aplica kSchema (Schema.hpp). Idempotente.
    void migrate();

private:
    sqlite3* db_ = nullptr;
};

}  // namespace todo
