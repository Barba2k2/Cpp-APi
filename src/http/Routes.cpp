#include "http/Routes.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <type_traits>

using namespace todo;

namespace {

  using json = nlohmann::json;

  void send(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
  }

  void send_error(httplib::Response& res, int status, const std::string& msg) {
    send(res, status, json{{"error", msg}});
  }

  // Resposta de um handler; body nulo = sem corpo.
  struct Reply {
    int status;
    json body = nullptr;
  };

  json parse_object(const httplib::Request& req) {
    json j = json::parse(req.body);
    if (!j.is_object()) throw ServiceError(400, "json object expected");
    return j;
  }

  std::string require_string(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
      throw ServiceError(400, std::string("field '") + key + "' must be a string");
    }
    return it->get<std::string>();
  }

  std::int64_t require_user(const httplib::Request& req, const AuthService& auth) {
    const std::string h = req.get_header_value("Authorization");
    constexpr const char* kPrefix = "Bearer ";
    if (h.rfind(kPrefix, 0) != 0 || h.size() == 7) throw ServiceError(401, "unauthorized");
    return auth.verify_token(h.substr(7));
  }

  std::int64_t path_id(const httplib::Request& req) {
    try {
      return std::stoll(req.matches[1].str());
    } catch (const std::exception&) {
      throw ServiceError(404, "task not found");
    }
  }

  // Converte excecoes em respostas; detalhes internos nunca saem na resposta.
  template <typename Fn> void guarded(httplib::Response& res, Fn&& fn) {
    try {
      const Reply r = fn();
      res.status = r.status;
      if (!r.body.is_null()) res.set_content(r.body.dump(), "application/json");
    } catch (const ServiceError& e) {
      send_error(res, e.status, e.what());
    } catch (const json::parse_error&) {
      send_error(res, 400, "malformed json");
    } catch (const json::exception&) {
      send_error(res, 400, "invalid json fields");
    } catch (...) {
      send_error(res, 500, "internal error");
    }
  }

  // Rota sem auth: fn recebe o body JSON (objeto) ou a request crua.
  template <typename Fn> auto open(Fn fn) {
    return [fn](const httplib::Request& req, httplib::Response& res) {
      guarded(res, [&] {
        if constexpr (std::is_invocable_v<Fn, const json&>)
          return fn(parse_object(req));
        else
          return fn(req);
      });
    };
  }

  // Rota autenticada: valida Bearer e chama fn(req, user_id).
  template <typename Fn> auto authed(const AuthService& auth, Fn fn) {
    return [&auth, fn](const httplib::Request& req, httplib::Response& res) {
      guarded(res, [&] { return fn(req, require_user(req, auth)); });
    };
  }

} // namespace

namespace todo {
  NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Task, id, title, done, created_at)
}

void todo::register_routes(httplib::Server& svr, AuthService& auth, TaskService& tasks) {
  svr.set_payload_max_length(64 * 1024);

  svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
    send_error(res, 500, "internal error");
  });

  svr.Post("/auth/register", open([&](const json& j) {
             auto id = auth.register_user(require_string(j, "email"), require_string(j, "password"));
             return Reply{201, {{"id", id}}};
           }));

  svr.Post("/auth/login", open([&](const json& j) {
             auto token = auth.login(require_string(j, "email"), require_string(j, "password"));
             return Reply{200, {{"token", token}}};
           }));

  svr.Post("/auth/forgot-password", open([&](const json& j) {
             auth.forgot_password(require_string(j, "email"));
             return Reply{200, {{"ok", true}}};
           }));

  svr.Post("/auth/reset-password", open([&](const json& j) {
             auth.reset_password(require_string(j, "token"), require_string(j, "new_password"));
             return Reply{200, {{"ok", true}}};
           }));

  svr.Get("/tasks", authed(auth, [&](const httplib::Request&, std::int64_t uid) {
            return Reply{200, json(tasks.list(uid))};
          }));

  svr.Post("/tasks", authed(auth, [&](const httplib::Request& req, std::int64_t uid) {
             return Reply{201, json(tasks.create(uid, require_string(parse_object(req), "title")))};
           }));

  svr.Get(R"(/tasks/(\d+))", authed(auth, [&](const httplib::Request& req, std::int64_t uid) {
            return Reply{200, json(tasks.get(uid, path_id(req)))};
          }));

  svr.Put(R"(/tasks/(\d+))", authed(auth, [&](const httplib::Request& req, std::int64_t uid) {
            const auto id = path_id(req);
            const json j = parse_object(req);
            std::optional<std::string> title;
            std::optional<bool> done;
            if (j.contains("title")) title = require_string(j, "title");
            if (j.contains("done")) {
              if (!j["done"].is_boolean()) throw ServiceError(400, "field 'done' must be a boolean");
              done = j["done"].get<bool>();
            }
            return Reply{200, json(tasks.update(uid, id, title, done))};
          }));

  svr.Delete(R"(/tasks/(\d+))", authed(auth, [&](const httplib::Request& req, std::int64_t uid) {
               tasks.remove(uid, path_id(req));
               return Reply{204};
             }));
}
