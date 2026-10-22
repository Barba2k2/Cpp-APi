#include <cstdlib>
#include <iostream>

#include "auth/AuthService.hpp"
#include "db/Database.hpp"
#include "http/Routes.hpp"
#include "mail/ConsoleMailSender.hpp"
#include "tasks/TaskService.hpp"

int main() {
  const char* secret = std::getenv("JWT_SECRET");
  if (!secret || std::string(secret).size() < 32) {
    std::cerr << "JWT_SECRET ausente ou < 32 chars\n";
    return 1;
  }
  const char* db_path = std::getenv("TODO_DB");
  const char* port_env = std::getenv("PORT");
  const int port = port_env ? std::atoi(port_env) : 8080;

  todo::Database db(db_path ? db_path : "todo.db");
  db.migrate();

  todo::ConsoleMailSender mail;
  todo::AuthService auth(db, mail, todo::AuthConfig{secret});
  todo::TaskService tasks(db);

  httplib::Server svr;
  todo::register_routes(svr, auth, tasks);

  std::cout << "listening on 127.0.0.1:" << port << std::endl;
  svr.listen("127.0.0.1", port);
}
