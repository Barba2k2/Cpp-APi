#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <functional>
#include <optional>
#include <string>

#include "db/Database.hpp"
#include "tasks/TaskService.hpp"

namespace {

  int status_of(const std::function<void()>& fn) {
    try {
      fn();
    } catch (const todo::ServiceError& e) {
      return e.status;
    }
    return 0;
  }

  std::int64_t add_user(todo::Database& db, const std::string& email) {
    db.exec("INSERT INTO users(email, password_hash, created_at) VALUES('" + email + "', 'x', 0)");
    return sqlite3_last_insert_rowid(db.handle());
  }

} // namespace

TEST_CASE("task CRUD", "[tasks]") {
  todo::Database db(":memory:");
  db.migrate();
  todo::TaskService svc(db);
  const auto u = add_user(db, "a@example.com");

  auto t = svc.create(u, "write tests");
  CHECK(t.id > 0);
  CHECK(t.title == "write tests");
  CHECK_FALSE(t.done);

  CHECK(svc.list(u).size() == 1);
  CHECK(svc.get(u, t.id).title == "write tests");

  auto upd = svc.update(u, t.id, std::nullopt, true);
  CHECK(upd.done);
  CHECK(upd.title == "write tests");
  upd = svc.update(u, t.id, std::string("renamed"), std::nullopt);
  CHECK(upd.title == "renamed");
  CHECK(upd.done);

  svc.remove(u, t.id);
  CHECK(svc.list(u).empty());
  CHECK(status_of([&] { svc.get(u, t.id); }) == 404);
}

TEST_CASE("invalid titles give 400", "[tasks]") {
  todo::Database db(":memory:");
  db.migrate();
  todo::TaskService svc(db);
  const auto u = add_user(db, "a@example.com");

  CHECK(status_of([&] { svc.create(u, ""); }) == 400);
  CHECK(status_of([&] { svc.create(u, std::string(201, 'x')); }) == 400);
  CHECK_NOTHROW(svc.create(u, std::string(200, 'x')));

  auto t = svc.create(u, "ok");
  CHECK(status_of([&] { svc.update(u, t.id, std::string(""), std::nullopt); }) == 400);
}

TEST_CASE("users are isolated from each other", "[tasks]") {
  todo::Database db(":memory:");
  db.migrate();
  todo::TaskService svc(db);
  const auto a = add_user(db, "a@example.com");
  const auto b = add_user(db, "b@example.com");

  auto ta = svc.create(a, "secret of a");
  svc.create(b, "b task");

  CHECK(svc.list(a).size() == 1);
  CHECK(svc.list(b).size() == 1);
  CHECK(svc.list(b)[0].title == "b task");

  CHECK(status_of([&] { svc.get(b, ta.id); }) == 404);
  CHECK(status_of([&] { svc.update(b, ta.id, std::string("hacked"), true); }) == 404);
  CHECK(status_of([&] { svc.remove(b, ta.id); }) == 404);
  CHECK(svc.get(a, ta.id).title == "secret of a");
}
