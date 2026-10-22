#pragma once
#include <stdexcept>
#include <string>

namespace todo {

// Erro de dominio com status HTTP. Camada http converte em {"error": msg}.
struct ServiceError : std::runtime_error {
    int status;
    ServiceError(int status, const std::string& msg) : std::runtime_error(msg), status(status) {}
};

}  // namespace todo
