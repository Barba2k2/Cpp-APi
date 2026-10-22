#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>

#include "auth/AuthService.hpp"
#include "db/Database.hpp"
#include "mail/IMailSender.hpp"

namespace {

  struct FakeMail : todo::IMailSender {
    std::string last_to;
    std::string last_token;
    int calls = 0;
    void send_password_reset(const std::string& to, const std::string& token) override {
      last_to = to;
      last_token = token;
      ++calls;
    }
  };

  const std::string kSecret(40, 's');

  int status_of(const std::function<void()>& fn) {
    try {
      fn();
    } catch (const todo::ServiceError& e) {
      return e.status;
    }
    return 0;
  }

  struct Fixture {
    todo::Database db{":memory:"};
    FakeMail mail;
    Fixture() { db.migrate(); }
    todo::AuthService make(int access_ttl = 900, int reset_ttl = 3600) {
      todo::AuthConfig cfg;
      cfg.jwt_secret = kSecret;
      cfg.access_ttl_seconds = access_ttl;
      cfg.reset_ttl_seconds = reset_ttl;
      return todo::AuthService(db, mail, cfg);
    }
  };

} // namespace

TEST_CASE("register and duplicate email", "[auth]") {
  Fixture f;
  auto auth = f.make();
  CHECK(auth.register_user("  Alice@Example.com ", "password123") > 0);
  CHECK(status_of([&] { auth.register_user("alice@example.com", "password123"); }) == 409);
  CHECK(status_of([&] { auth.register_user("not-an-email", "password123"); }) == 400);
  CHECK(status_of([&] { auth.register_user("bob@example.com", "short"); }) == 400);
}

TEST_CASE("secret too short throws", "[auth]") {
  Fixture f;
  todo::AuthConfig cfg;
  cfg.jwt_secret = "short";
  CHECK_THROWS(todo::AuthService(f.db, f.mail, cfg));
}

TEST_CASE("login and verify_token", "[auth]") {
  Fixture f;
  auto auth = f.make();
  auto id = auth.register_user("alice@example.com", "password123");

  auto token = auth.login("ALICE@example.com", "password123");
  CHECK(auth.verify_token(token) == id);

  CHECK(status_of([&] { auth.login("alice@example.com", "wrong-password"); }) == 401);
  CHECK(status_of([&] { auth.login("nobody@example.com", "password123"); }) == 401);
  CHECK(status_of([&] { auth.verify_token("garbage"); }) == 401);

  auto tampered = token;
  tampered.back() = tampered.back() == 'A' ? 'B' : 'A';
  CHECK(status_of([&] { auth.verify_token(tampered); }) == 401);
}

TEST_CASE("expired access token is rejected", "[auth]") {
  Fixture f;
  auto auth = f.make(-60);
  auth.register_user("alice@example.com", "password123");
  auto token = auth.login("alice@example.com", "password123");
  CHECK(status_of([&] { auth.verify_token(token); }) == 401);
}

TEST_CASE("token signed with another secret is rejected", "[auth]") {
  Fixture f;
  auto auth = f.make();
  auth.register_user("alice@example.com", "password123");
  auto token = auth.login("alice@example.com", "password123");

  todo::Database db2{":memory:"};
  db2.migrate();
  todo::AuthConfig cfg;
  cfg.jwt_secret = std::string(40, 'x');
  todo::AuthService other(db2, f.mail, cfg);
  CHECK(status_of([&] { other.verify_token(token); }) == 401);
}

TEST_CASE("forgot and reset password flow", "[auth]") {
  Fixture f;
  auto auth = f.make();
  auth.register_user("alice@example.com", "password123");

  auth.forgot_password("ghost@example.com");
  CHECK(f.mail.calls == 0);

  auth.forgot_password("alice@example.com");
  REQUIRE(f.mail.calls == 1);
  CHECK(f.mail.last_to == "alice@example.com");
  const std::string token = f.mail.last_token;
  REQUIRE_FALSE(token.empty());

  CHECK(status_of([&] { auth.reset_password(token, "short"); }) == 400);

  auth.reset_password(token, "new-password-1");
  CHECK(status_of([&] { auth.login("alice@example.com", "password123"); }) == 401);
  CHECK_NOTHROW(auth.login("alice@example.com", "new-password-1"));

  CHECK(status_of([&] { auth.reset_password(token, "another-pass-2"); }) == 401);
  CHECK(status_of([&] { auth.reset_password("bogus", "another-pass-2"); }) == 401);
}

TEST_CASE("expired reset token is rejected", "[auth]") {
  Fixture f;
  auto auth = f.make(900, -10);
  auth.register_user("alice@example.com", "password123");
  auth.forgot_password("alice@example.com");
  REQUIRE(f.mail.calls == 1);
  CHECK(status_of([&] { auth.reset_password(f.mail.last_token, "new-password-1"); }) == 401);
  CHECK_NOTHROW(auth.login("alice@example.com", "password123"));
}
