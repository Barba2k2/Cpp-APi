#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/Errors.hpp"
#include "db/Database.hpp"

namespace todo {

  struct Task {
    std::int64_t id = 0;
    std::string title;
    bool done = false;
    std::int64_t created_at = 0;
  };

  // Dono: Copilot. Toda query filtra por user_id (isolamento entre usuarios).
  // Erros: 400 titulo vazio/> 200 chars, 404 task inexistente OU de outro usuario.
  class TaskService {
  public:
    explicit TaskService(Database& db) : db_(db) {}

    Task create(std::int64_t user_id, const std::string& title);
    std::vector<Task> list(std::int64_t user_id);
    Task get(std::int64_t user_id, std::int64_t id);
    // Campos ausentes (nullopt) nao mudam.
    Task update(std::int64_t user_id, std::int64_t id, const std::optional<std::string>& title,
                const std::optional<bool>& done);
    void remove(std::int64_t user_id, std::int64_t id);

  private:
    Database& db_;
  };

} // namespace todo
