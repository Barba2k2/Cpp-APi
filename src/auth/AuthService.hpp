#pragma once
#include <cstdint>
#include <string>

#include "common/Errors.hpp"
#include "db/Database.hpp"
#include "mail/IMailSender.hpp"

namespace todo {

  struct AuthConfig {
    std::string jwt_secret;       // >= 32 chars; construtor lanca se menor
    int access_ttl_seconds = 900; // 15 min
    int reset_ttl_seconds = 3600; // 1 h
  };

  // Dono: Codex. Chama sodium_init() no construtor.
  //
  // Regras:
  //  - email normalizado (trim + lowercase), validacao basica de formato.
  //  - senha >= 8 chars; hash com crypto_pwhash_str (argon2id).
  //  - JWT HS256, claims: sub=<user_id>, iat, exp. Sem dados sensiveis.
  //  - forgot_password: gera token aleatorio (32 bytes, hex), guarda so SHA-256 em
  //    password_resets, chama mail.send_password_reset. NUNCA revela se o email existe.
  //  - reset_password: token valido, nao expirado, nao usado; marca used=1 e troca hash.
  //  - login/erros de credencial: mesma mensagem generica (anti-enumeracao).
  //
  // Erros (ServiceError.status): 400 entrada invalida, 401 credencial/token invalido,
  // 409 email ja cadastrado.
  class AuthService {
  public:
    AuthService(Database& db, IMailSender& mail, AuthConfig cfg);

    // Retorna user_id.
    std::int64_t register_user(const std::string& email, const std::string& password);

    // Retorna JWT de acesso.
    std::string login(const std::string& email, const std::string& password);

    // Sempre retorna sem erro para email inexistente.
    void forgot_password(const std::string& email);

    void reset_password(const std::string& token, const std::string& new_password);

    // Valida assinatura + exp. Retorna user_id. Lanca ServiceError(401).
    std::int64_t verify_token(const std::string& jwt) const;

  private:
    Database& db_;
    IMailSender& mail_;
    AuthConfig cfg_;
  };

} // namespace todo
