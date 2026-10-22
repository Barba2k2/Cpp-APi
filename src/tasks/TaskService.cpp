#include "tasks/TaskService.hpp"

#include <algorithm>
#include <chrono>

#include "common/Strings.hpp"

using namespace todo;

namespace {

  // Conta code points UTF-8 (ignora bytes de continuacao).
  std::string valid_title(const std::string& raw) {
    std::string t = trim(raw);
    auto chars = std::count_if(t.begin(), t.end(), [](unsigned char c) { return (c & 0xC0) != 0x80; });
    if (chars < 1 || chars > 200) {
      throw ServiceError(400, "title must be 1 to 200 characters");
    }
    return t;
  }

  Task read_row(SQLite::Statement& q) {
    return Task{q.getColumn(0).getInt64(), q.getColumn(1).getString(), q.getColumn(2).getInt() != 0,
                q.getColumn(3).getInt64()};
  }

} // namespace

Task TaskService::create(std::int64_t user_id, const std::string& title) {
  const std::string t = valid_title(title);
  const auto now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
          .count();
  SQLite::Statement q(db_.db(), "INSERT INTO tasks(user_id, title, done, created_at) VALUES(?1, ?2, 0, ?3)");
  q.bind(1, user_id);
  q.bind(2, t);
  q.bind(3, static_cast<std::int64_t>(now));
  q.exec();
  return get(user_id, db_.db().getLastInsertRowid());
}

std::vector<Task> TaskService::list(std::int64_t user_id) {
  SQLite::Statement q(db_.db(), "SELECT id, title, done, created_at FROM tasks WHERE user_id=?1 ORDER BY id");
  q.bind(1, user_id);
  std::vector<Task> out;
  while (q.executeStep())
    out.push_back(read_row(q));
  return out;
}

Task TaskService::get(std::int64_t user_id, std::int64_t id) {
  SQLite::Statement q(db_.db(), "SELECT id, title, done, created_at FROM tasks WHERE id=?1 AND user_id=?2");
  q.bind(1, id);
  q.bind(2, user_id);
  if (!q.executeStep()) throw ServiceError(404, "task not found");
  return read_row(q);
}

Task TaskService::update(std::int64_t user_id, std::int64_t id, const std::optional<std::string>& title,
                         const std::optional<bool>& done) {
  std::optional<std::string> t;
  if (title) t = valid_title(*title);
  get(user_id, id); // 404 antes de qualquer escrita

  SQLite::Statement q(db_.db(), "UPDATE tasks SET title=COALESCE(?1, title), done=COALESCE(?2, done) "
                                "WHERE id=?3 AND user_id=?4");
  if (t)
    q.bind(1, *t);
  else
    q.bind(1);
  if (done)
    q.bind(2, *done ? 1 : 0);
  else
    q.bind(2);
  q.bind(3, id);
  q.bind(4, user_id);
  q.exec();
  return get(user_id, id);
}

void TaskService::remove(std::int64_t user_id, std::int64_t id) {
  SQLite::Statement q(db_.db(), "DELETE FROM tasks WHERE id=?1 AND user_id=?2");
  q.bind(1, id);
  q.bind(2, user_id);
  q.exec();
  if (db_.db().getChanges() == 0) throw ServiceError(404, "task not found");
}
