#include <catch2/catch_test_macros.hpp>

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <thread>

#include "auth/AuthService.hpp"
#include "db/Database.hpp"
#include "http/Routes.hpp"
#include "mail/IMailSender.hpp"
#include "tasks/TaskService.hpp"

namespace {

  using json = nlohmann::json;

  struct NullMail : todo::IMailSender {
    void send_password_reset(const std::string&, const std::string&) override {}
  };

  struct Server {
    todo::Database db{":memory:"};
    NullMail mail;
    std::unique_ptr<todo::AuthService> auth;
    std::unique_ptr<todo::TaskService> tasks;
    httplib::Server svr;
    std::thread th;
    int port = 0;

    Server() {
      db.migrate();
      todo::AuthConfig cfg;
      cfg.jwt_secret = std::string(40, 'k');
      auth = std::make_unique<todo::AuthService>(db, mail, cfg);
      tasks = std::make_unique<todo::TaskService>(db);
      todo::register_routes(svr, *auth, *tasks);
      port = svr.bind_to_any_port("127.0.0.1");
      th = std::thread([this] { svr.listen_after_bind(); });
      svr.wait_until_ready();
    }
    ~Server() {
      svr.stop();
      if (th.joinable()) th.join();
    }
  };

} // namespace

TEST_CASE("http flow: register, login, create and list tasks", "[http]") {
  Server s;
  httplib::Client cli("127.0.0.1", s.port);
  const std::string ct = "application/json";

  auto reg =
      cli.Post("/auth/register", json{{"email", "a@example.com"}, {"password", "password123"}}.dump(), ct);
  REQUIRE(reg);
  CHECK(reg->status == 201);

  auto dup =
      cli.Post("/auth/register", json{{"email", "a@example.com"}, {"password", "password123"}}.dump(), ct);
  REQUIRE(dup);
  CHECK(dup->status == 409);

  auto bad =
      cli.Post("/auth/login", json{{"email", "a@example.com"}, {"password", "wrong-password"}}.dump(), ct);
  REQUIRE(bad);
  CHECK(bad->status == 401);

  auto login =
      cli.Post("/auth/login", json{{"email", "a@example.com"}, {"password", "password123"}}.dump(), ct);
  REQUIRE(login);
  REQUIRE(login->status == 200);
  const std::string token = json::parse(login->body).at("token");

  httplib::Headers auth{{"Authorization", "Bearer " + token}};

  auto created = cli.Post("/tasks", auth, json{{"title", "first"}}.dump(), ct);
  REQUIRE(created);
  CHECK(created->status == 201);
  CHECK(json::parse(created->body).at("title") == "first");

  auto invalid = cli.Post("/tasks", auth, json{{"title", ""}}.dump(), ct);
  REQUIRE(invalid);
  CHECK(invalid->status == 400);

  auto list = cli.Get("/tasks", auth);
  REQUIRE(list);
  CHECK(list->status == 200);
  auto arr = json::parse(list->body);
  REQUIRE(arr.is_array());
  CHECK(arr.size() == 1);
  CHECK(arr[0].at("title") == "first");
}

TEST_CASE("http: /tasks requires a valid token", "[http]") {
  Server s;
  httplib::Client cli("127.0.0.1", s.port);

  auto none = cli.Get("/tasks");
  REQUIRE(none);
  CHECK(none->status == 401);

  auto badtok = cli.Get("/tasks", httplib::Headers{{"Authorization", "Bearer not.a.token"}});
  REQUIRE(badtok);
  CHECK(badtok->status == 401);

  auto post = cli.Post("/tasks", json{{"title", "x"}}.dump(), "application/json");
  REQUIRE(post);
  CHECK(post->status == 401);
}

TEST_CASE("http: malformed json gives 400", "[http]") {
  Server s;
  httplib::Client cli("127.0.0.1", s.port);
  auto r = cli.Post("/auth/register", "{not json", "application/json");
  REQUIRE(r);
  CHECK(r->status == 400);
}
