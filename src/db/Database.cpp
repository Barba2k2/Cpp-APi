#include "db/Database.hpp"

#include <stdexcept>

#include "db/Schema.hpp"

using namespace todo;

Database::Database(const std::string& path) : db_(path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) {
  db_.setBusyTimeout(5000);
}

void Database::migrate() {
  exec("PRAGMA foreign_keys=ON;");
  exec(kSchema);
}
