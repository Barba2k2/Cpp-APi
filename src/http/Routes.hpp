#pragma once
#include <httplib.h>

#include "auth/AuthService.hpp"
#include "tasks/TaskService.hpp"

namespace todo {

// Dono: Copilot.
//
// Rotas:
//   POST /auth/register         {email,password}      -> 201 {"id":N}
//   POST /auth/login            {email,password}      -> 200 {"token":"..."}
//   POST /auth/forgot-password  {email}               -> 200 {"ok":true} (sempre)
//   POST /auth/reset-password   {token,new_password}  -> 200 {"ok":true}
//   GET    /tasks               -> 200 [Task]
//   POST   /tasks               {title}               -> 201 Task
//   GET    /tasks/:id           -> 200 Task
//   PUT    /tasks/:id           {title?,done?}        -> 200 Task
//   DELETE /tasks/:id           -> 204
//
// /tasks/* exige "Authorization: Bearer <jwt>" (senao 401).
// Task JSON: {"id","title","done","created_at"}.
// Erro JSON: {"error":"msg"} com ServiceError.status. JSON malformado -> 400.
// Excecao inesperada -> 500 {"error":"internal error"} sem vazar detalhe.
void register_routes(httplib::Server& svr, AuthService& auth, TaskService& tasks);

}  // namespace todo
