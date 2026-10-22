#include "auth/AuthService.hpp"

#include <sodium.h>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>

#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include "common/Strings.hpp"

using namespace todo;

namespace {

  std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  std::string normalize_email(const std::string& raw) {
    std::string e = trim(raw);
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
  }

  bool valid_email(const std::string& e) {
    if (e.empty() || e.size() > 254) return false;
    if (std::any_of(e.begin(), e.end(), [](unsigned char c) { return std::isspace(c) || c < 0x20; }))
      return false;
    auto at = e.find('@');
    if (at == std::string::npos || at == 0 || at != e.rfind('@')) return false;
    auto domain = e.substr(at + 1);
    auto dot = domain.find('.');
    return dot != std::string::npos && dot > 0 && domain.back() != '.';
  }

  std::string hash_password(const std::string& pw) {
    char out[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(out, pw.c_str(), pw.size(), crypto_pwhash_OPSLIMIT_INTERACTIVE,
                          crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
      throw std::runtime_error("password hashing failed");
    }
    return out;
  }

  bool verify_password(const std::string& hash, const std::string& pw) {
    return crypto_pwhash_str_verify(hash.c_str(), pw.c_str(), pw.size()) == 0;
  }

  std::string to_hex(const unsigned char* data, std::size_t len) {
    std::string out(len * 2 + 1, '\0');
    sodium_bin2hex(out.data(), out.size(), data, len);
    out.resize(len * 2);
    return out;
  }

  std::string sha256_hex(const std::string& in) {
    unsigned char h[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(h, reinterpret_cast<const unsigned char*>(in.data()), in.size());
    return to_hex(h, sizeof h);
  }

  void check_new_password(const std::string& pw) {
    if (pw.size() < 8) throw ServiceError(400, "password must be at least 8 characters");
  }

  const char* const kBadCredentials = "invalid credentials";
  const char* const kBadResetToken = "invalid or expired token";

} // namespace

AuthService::AuthService(Database& db, IMailSender& mail, AuthConfig cfg)
    : db_(db), mail_(mail), cfg_(std::move(cfg)) {
  if (sodium_init() < 0) throw std::runtime_error("sodium_init failed");
  if (cfg_.jwt_secret.size() < 32) throw std::runtime_error("jwt_secret must be >= 32 chars");
}

std::int64_t AuthService::register_user(const std::string& email, const std::string& password) {
  const std::string e = normalize_email(email);
  if (!valid_email(e)) throw ServiceError(400, "invalid email");
  check_new_password(password);
  const std::string hash = hash_password(password);

  try {
    SQLite::Statement q(db_.db(), "INSERT INTO users(email, password_hash, created_at) VALUES(?1, ?2, ?3)");
    q.bind(1, e);
    q.bind(2, hash);
    q.bind(3, now_seconds());
    q.exec();
    return db_.db().getLastInsertRowid();
  } catch (const SQLite::Exception& ex) {
    if (ex.getExtendedErrorCode() == SQLITE_CONSTRAINT_UNIQUE || ex.getErrorCode() == SQLITE_CONSTRAINT) {
      throw ServiceError(409, "email already registered");
    }
    throw;
  }
}

std::string AuthService::login(const std::string& email, const std::string& password) {
  const std::string e = normalize_email(email);
  std::int64_t id = 0;
  std::string hash;
  {
    SQLite::Statement q(db_.db(), "SELECT id, password_hash FROM users WHERE email=?1");
    q.bind(1, e);
    if (q.executeStep()) {
      id = q.getColumn(0).getInt64();
      hash = q.getColumn(1).getString();
    }
  }
  if (id == 0) {
    // Hash dummy para igualar o tempo de resposta de email inexistente.
    static const std::string dummy = hash_password("dummy-password-for-timing");
    verify_password(dummy, password);
    throw ServiceError(401, kBadCredentials);
  }
  if (!verify_password(hash, password)) throw ServiceError(401, kBadCredentials);

  const auto now = std::chrono::system_clock::now();
  return jwt::create()
      .set_type("JWT")
      .set_subject(std::to_string(id))
      .set_issued_at(now)
      .set_expires_at(now + std::chrono::seconds(cfg_.access_ttl_seconds))
      .sign(jwt::algorithm::hs256{cfg_.jwt_secret});
}

void AuthService::forgot_password(const std::string& email) {
  const std::string e = normalize_email(email);
  if (!valid_email(e)) return;

  try {
    std::int64_t user_id = 0;
    {
      SQLite::Statement q(db_.db(), "SELECT id FROM users WHERE email=?1");
      q.bind(1, e);
      if (q.executeStep()) user_id = q.getColumn(0).getInt64();
    }
    if (user_id == 0) return;

    unsigned char raw[32];
    randombytes_buf(raw, sizeof raw);
    const std::string token = to_hex(raw, sizeof raw);
    sodium_memzero(raw, sizeof raw);

    SQLite::Statement q(db_.db(), "INSERT INTO password_resets(user_id, token_hash, expires_at, used) "
                                  "VALUES(?1, ?2, ?3, 0)");
    q.bind(1, user_id);
    q.bind(2, sha256_hex(token));
    q.bind(3, now_seconds() + cfg_.reset_ttl_seconds);
    q.exec();
    mail_.send_password_reset(e, token);
  } catch (...) {
    // Falhas aqui nao podem diferenciar email existente de inexistente.
  }
}

void AuthService::reset_password(const std::string& token, const std::string& new_password) {
  check_new_password(new_password);
  const std::string token_hash = sha256_hex(token);
  const std::string new_hash = hash_password(new_password);

  SQLite::Transaction tx(db_.db(), SQLite::TransactionBehavior::IMMEDIATE);
  std::int64_t reset_id = 0, user_id = 0;
  {
    SQLite::Statement q(db_.db(), "SELECT id, user_id FROM password_resets "
                                  "WHERE token_hash=?1 AND used=0 AND expires_at>?2");
    q.bind(1, token_hash);
    q.bind(2, now_seconds());
    if (!q.executeStep()) throw ServiceError(401, kBadResetToken);
    reset_id = q.getColumn(0).getInt64();
    user_id = q.getColumn(1).getInt64();
  }
  {
    SQLite::Statement q(db_.db(), "UPDATE password_resets SET used=1 WHERE id=?1 AND used=0");
    q.bind(1, reset_id);
    q.exec();
    if (db_.db().getChanges() != 1) throw ServiceError(401, kBadResetToken);
  }
  {
    SQLite::Statement q(db_.db(), "UPDATE users SET password_hash=?1 WHERE id=?2");
    q.bind(1, new_hash);
    q.bind(2, user_id);
    q.exec();
  }
  tx.commit();
}

std::int64_t AuthService::verify_token(const std::string& token) const {
  try {
    auto decoded = jwt::decode(token);
    jwt::verify().allow_algorithm(jwt::algorithm::hs256{cfg_.jwt_secret}).verify(decoded);
    const std::string sub = decoded.get_subject();
    std::size_t pos = 0;
    const long long id = std::stoll(sub, &pos);
    if (pos != sub.size() || id <= 0) throw ServiceError(401, "invalid token");
    return id;
  } catch (const ServiceError&) {
    throw;
  } catch (...) {
    throw ServiceError(401, "invalid token");
  }
}
