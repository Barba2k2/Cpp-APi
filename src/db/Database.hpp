#pragma once
#include <SQLiteCpp/SQLiteCpp.h>

#include <string>

namespace todo {

  // Wrapper fino sobre SQLite::Database (SQLiteCpp). Statement/Transaction vem de db().
  class Database {
  public:
    // ":memory:" permitido (testes). Lanca SQLite::Exception (std::runtime_error) se falhar.
    explicit Database(const std::string& path);

    SQLite::Database& db() { return db_; }
    sqlite3* handle() const noexcept { return db_.getHandle(); }

    // Executa SQL sem resultado. Lanca SQLite::Exception.
    void exec(const std::string& sql) { db_.exec(sql); }

    // PRAGMA foreign_keys=ON + aplica kSchema (Schema.hpp). Idempotente.
    void migrate();

  private:
    SQLite::Database db_;
  };

} // namespace todo
