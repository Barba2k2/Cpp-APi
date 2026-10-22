#pragma once
#include <iostream>

#include "mail/IMailSender.hpp"

namespace todo {

  // Imprime o token no console (dev). Header-only.
  class ConsoleMailSender : public IMailSender {
  public:
    void send_password_reset(const std::string& to_email, const std::string& token) override {
      std::cout << "[mail] reset para " << to_email << " token=" << token << std::endl;
    }
  };

} // namespace todo
