#pragma once
#include <string>

namespace todo {

// Sem SMTP no escopo. Troca de impl nao afeta a API.
class IMailSender {
public:
    virtual ~IMailSender() = default;
    virtual void send_password_reset(const std::string& to_email, const std::string& token) = 0;
};

}  // namespace todo
