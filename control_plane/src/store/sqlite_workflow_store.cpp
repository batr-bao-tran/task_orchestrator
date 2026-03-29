#include "control_plane/store/sqlite_workflow_store.hpp"

#include <SQLiteCpp/Column.h>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace task_orchestrator::control_plane::store {
namespace {

constexpr std::size_t kDefaultPageSize = 50;
constexpr int kBusyTimeoutMs = 5000;
constexpr const char* kUnknownBootId = "boot-id-unavailable";

std::string read_kernel_boot_id() {
  std::ifstream input("/proc/sys/kernel/random/boot_id");
  std::string boot_id;
  if (!std::getline(input, boot_id)) {
    return kUnknownBootId;
  }
  while (!boot_id.empty() && std::isspace(static_cast<unsigned char>(boot_id.back())) != 0) {
    boot_id.pop_back();
  }
  return boot_id.empty() ? std::string(kUnknownBootId) : boot_id;
}

std::string serialize_proto(const google::protobuf::Message& message) { return message.SerializeAsString(); }

template <typename Message>
Message parse_proto_blob(const SQLite::Column& column) {
  Message message;
  const std::string blob = column.getString();
  if (!message.ParseFromArray(blob.data(), static_cast<int>(blob.size()))) {
    throw std::runtime_error("Failed to parse protobuf payload from SQLite blob column.");
  }
  return message;
}

void bind_blob(SQLite::Statement& statement, const int index, const std::string& blob) {
  statement.bind(index, blob.data(), static_cast<int>(blob.size()));
}

std::size_t parse_page_token(std::string_view page_token) {
  if (page_token.empty()) {
    return 0;
  }
  try {
    return static_cast<std::size_t>(std::stoull(std::string(page_token)));
  } catch (const std::exception&) {
    return 0;
  }
}

std::size_t normalize_page_size(const std::int32_t requested_page_size) {
  if (requested_page_size <= 0) {
    return kDefaultPageSize;
  }
  return static_cast<std::size_t>(requested_page_size);
}

std::string make_state_placeholders(const int count) {
  std::string placeholders;
  for (int index = 0; index < count; ++index) {
    if (!placeholders.empty()) {
      placeholders += ", ";
    }
    placeholders += "?";
  }
  return placeholders;
}

void bind_state_filters(SQLite::Statement& statement,
                        int* bind_index,
                        const google::protobuf::RepeatedField<int>& states) {
  for (const int state : states) {
    statement.bind((*bind_index)++, state);
  }
}

protocol::WorkflowSummary read_summary_row(SQLite::Statement& statement, const int first_column = 0) {
  protocol::WorkflowSummary summary;
  summary.set_workflow_id(statement.getColumn(first_column + 0).getString());
  summary.set_state(static_cast<protocol::WorkflowLifecycleState>(statement.getColumn(first_column + 1).getInt()));
  summary.set_created_at_unix_ms(statement.getColumn(first_column + 2).getInt64());
  summary.set_updated_at_unix_ms(statement.getColumn(first_column + 3).getInt64());
  summary.set_latest_plan_version(statement.getColumn(first_column + 4).getInt64());
  summary.set_total_event_count(statement.getColumn(first_column + 5).getInt64());
  summary.set_total_audit_entry_count(statement.getColumn(first_column + 6).getInt64());
  summary.set_last_error_message(statement.getColumn(first_column + 7).getString());
  return summary;
}

protocol::WorkflowRecord read_workflow_row(SQLite::Statement& statement) {
  protocol::WorkflowRecord record;
  *record.mutable_summary() = read_summary_row(statement, 0);
  *record.mutable_config() = parse_proto_blob<protocol::pb::WorkflowConfig>(statement.getColumn(8));
  if (statement.getColumn(9).getInt() != 0 && !statement.getColumn(10).isNull()) {
    *record.mutable_latest_response() = parse_proto_blob<protocol::pb::RuntimeApiResponse>(statement.getColumn(10));
  }
  return record;
}

protocol::WorkflowEventRecord read_event_row(SQLite::Statement& statement) {
  protocol::WorkflowEventRecord record;
  record.set_sequence(statement.getColumn(0).getInt64());
  record.set_recorded_at_unix_ms(statement.getColumn(1).getInt64());
  *record.mutable_event() = parse_proto_blob<protocol::pb::WorkflowEvent>(statement.getColumn(2));
  return record;
}

protocol::WorkflowPlanVersion read_plan_row(SQLite::Statement& statement) {
  protocol::WorkflowPlanVersion record;
  record.set_version(statement.getColumn(0).getInt64());
  record.set_recorded_at_unix_ms(statement.getColumn(1).getInt64());
  *record.mutable_response() = parse_proto_blob<protocol::pb::RuntimeApiResponse>(statement.getColumn(2));
  return record;
}

protocol::AuditEntry read_audit_row(SQLite::Statement& statement) {
  protocol::AuditEntry record;
  record.set_sequence(statement.getColumn(0).getInt64());
  record.set_recorded_at_unix_ms(statement.getColumn(1).getInt64());
  *record.mutable_actor() = statement.getColumn(2).getString();
  *record.mutable_action() = statement.getColumn(3).getString();
  *record.mutable_detail() = statement.getColumn(4).getString();
  return record;
}

protocol::IdempotencyRecord read_idempotency_row(SQLite::Statement& statement) {
  protocol::IdempotencyRecord record;
  record.set_key(statement.getColumn(0).getString());
  record.set_workflow_id(statement.getColumn(1).getString());
  record.set_request_fingerprint(statement.getColumn(2).getString());
  record.set_recorded_at_unix_ms(statement.getColumn(3).getInt64());
  *record.mutable_cached_response() = parse_proto_blob<protocol::pb::RuntimeApiResponse>(statement.getColumn(4));
  return record;
}

void run_checkpoint(SQLite::Database& database) {
  SQLite::Statement checkpoint(database, "PRAGMA wal_checkpoint(TRUNCATE);");
  while (checkpoint.executeStep()) {
  }
}

}  // namespace

SqliteWorkflowStore::SqliteWorkflowStore(std::filesystem::path database_path, std::string current_boot_id)
    : database_path_(std::move(database_path)),
      current_boot_id_(current_boot_id.empty() ? read_kernel_boot_id() : std::move(current_boot_id)) {
  if (database_path_.has_parent_path()) {
    std::filesystem::create_directories(database_path_.parent_path());
  }
  database_ = std::make_unique<SQLite::Database>(
      database_path_.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE, kBusyTimeoutMs);
  configure_database();
  initialize_schema();
}

SqliteWorkflowStore::~SqliteWorkflowStore() noexcept = default;

void SqliteWorkflowStore::configure_database() {
  database_->exec("PRAGMA foreign_keys = ON;");
  database_->exec("PRAGMA journal_mode = WAL;");
  database_->exec("PRAGMA synchronous = NORMAL;");
  database_->exec("PRAGMA temp_store = MEMORY;");
  database_->exec("PRAGMA wal_autocheckpoint = 1000;");
}

void SqliteWorkflowStore::initialize_schema() {
  const bool new_database = !database_->tableExists("workflow_records");
  if (new_database) {
    database_->exec("PRAGMA auto_vacuum = INCREMENTAL;");
  }

  SQLite::Transaction transaction(*database_);

  database_->exec(R"sql(
    CREATE TABLE IF NOT EXISTS workflow_records (
      workflow_id TEXT PRIMARY KEY,
      state INTEGER NOT NULL,
      created_at_unix_ms INTEGER NOT NULL,
      updated_at_unix_ms INTEGER NOT NULL,
      latest_plan_version INTEGER NOT NULL DEFAULT 0,
      total_event_count INTEGER NOT NULL DEFAULT 0,
      total_audit_entry_count INTEGER NOT NULL DEFAULT 0,
      last_error_message TEXT NOT NULL DEFAULT '',
      config_proto BLOB NOT NULL,
      has_latest_response INTEGER NOT NULL DEFAULT 0,
      latest_response_proto BLOB
    )
  )sql");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_workflow_records_updated ON workflow_records(updated_at_unix_ms DESC, "
      "workflow_id);");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_workflow_records_state_updated ON workflow_records(state, updated_at_unix_ms "
      "DESC, workflow_id);");

  database_->exec(R"sql(
    CREATE TABLE IF NOT EXISTS workflow_events (
      workflow_id TEXT NOT NULL,
      sequence INTEGER NOT NULL,
      recorded_at_unix_ms INTEGER NOT NULL,
      boot_id TEXT NOT NULL,
      event_type INTEGER NOT NULL,
      task_id TEXT NOT NULL DEFAULT '',
      actor_id TEXT NOT NULL DEFAULT '',
      detail TEXT NOT NULL DEFAULT '',
      event_proto BLOB NOT NULL,
      PRIMARY KEY (workflow_id, sequence),
      FOREIGN KEY (workflow_id) REFERENCES workflow_records(workflow_id) ON DELETE CASCADE
    ) WITHOUT ROWID
  )sql");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_workflow_events_prune ON workflow_events(boot_id, recorded_at_unix_ms);");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_workflow_events_recorded ON workflow_events(recorded_at_unix_ms DESC);");

  database_->exec(R"sql(
    CREATE TABLE IF NOT EXISTS workflow_plan_versions (
      workflow_id TEXT NOT NULL,
      version INTEGER NOT NULL,
      recorded_at_unix_ms INTEGER NOT NULL,
      boot_id TEXT NOT NULL,
      response_proto BLOB NOT NULL,
      PRIMARY KEY (workflow_id, version),
      FOREIGN KEY (workflow_id) REFERENCES workflow_records(workflow_id) ON DELETE CASCADE
    ) WITHOUT ROWID
  )sql");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_workflow_plan_versions_prune ON workflow_plan_versions(boot_id, "
      "recorded_at_unix_ms);");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_workflow_plan_versions_recorded ON workflow_plan_versions(recorded_at_unix_ms "
      "DESC);");

  database_->exec(R"sql(
    CREATE TABLE IF NOT EXISTS workflow_audit_entries (
      workflow_id TEXT NOT NULL,
      sequence INTEGER NOT NULL,
      recorded_at_unix_ms INTEGER NOT NULL,
      boot_id TEXT NOT NULL,
      actor TEXT NOT NULL,
      action TEXT NOT NULL,
      detail TEXT NOT NULL,
      audit_proto BLOB NOT NULL,
      PRIMARY KEY (workflow_id, sequence),
      FOREIGN KEY (workflow_id) REFERENCES workflow_records(workflow_id) ON DELETE CASCADE
    ) WITHOUT ROWID
  )sql");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_workflow_audit_entries_prune ON workflow_audit_entries(boot_id, "
      "recorded_at_unix_ms);");

  database_->exec(R"sql(
    CREATE TABLE IF NOT EXISTS idempotency_records (
      key TEXT PRIMARY KEY,
      workflow_id TEXT NOT NULL,
      request_fingerprint TEXT NOT NULL,
      recorded_at_unix_ms INTEGER NOT NULL,
      boot_id TEXT NOT NULL,
      cached_response_proto BLOB NOT NULL,
      FOREIGN KEY (workflow_id) REFERENCES workflow_records(workflow_id) ON DELETE CASCADE
    )
  )sql");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_idempotency_records_workflow ON idempotency_records(workflow_id, "
      "recorded_at_unix_ms DESC);");
  database_->exec(
      "CREATE INDEX IF NOT EXISTS idx_idempotency_records_prune ON idempotency_records(boot_id, recorded_at_unix_ms);");

  transaction.commit();

  if (new_database) {
    database_->exec("VACUUM;");
  }
  database_->exec("PRAGMA optimize;");
}

void SqliteWorkflowStore::upsert_workflow(const protocol::WorkflowRecord& workflow) {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    INSERT INTO workflow_records (
      workflow_id,
      state,
      created_at_unix_ms,
      updated_at_unix_ms,
      latest_plan_version,
      total_event_count,
      total_audit_entry_count,
      last_error_message,
      config_proto,
      has_latest_response,
      latest_response_proto
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(workflow_id) DO UPDATE SET
      state = excluded.state,
      created_at_unix_ms = excluded.created_at_unix_ms,
      updated_at_unix_ms = excluded.updated_at_unix_ms,
      latest_plan_version = excluded.latest_plan_version,
      total_event_count = excluded.total_event_count,
      total_audit_entry_count = excluded.total_audit_entry_count,
      last_error_message = excluded.last_error_message,
      config_proto = excluded.config_proto,
      has_latest_response = excluded.has_latest_response,
      latest_response_proto = excluded.latest_response_proto
  )sql");

  const std::string config_blob = serialize_proto(workflow.config());
  statement.bind(1, workflow.summary().workflow_id());
  statement.bind(2, static_cast<int>(workflow.summary().state()));
  statement.bind(3, workflow.summary().created_at_unix_ms());
  statement.bind(4, workflow.summary().updated_at_unix_ms());
  statement.bind(5, workflow.summary().latest_plan_version());
  statement.bind(6, workflow.summary().total_event_count());
  statement.bind(7, workflow.summary().total_audit_entry_count());
  statement.bind(8, workflow.summary().last_error_message());
  bind_blob(statement, 9, config_blob);
  statement.bind(10, workflow.has_latest_response() ? 1 : 0);
  if (workflow.has_latest_response()) {
    bind_blob(statement, 11, serialize_proto(workflow.latest_response()));
  } else {
    statement.bind(11);
  }
  statement.exec();
}

std::optional<protocol::WorkflowRecord> SqliteWorkflowStore::get_workflow(std::string_view workflow_id) const {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    SELECT workflow_id,
           state,
           created_at_unix_ms,
           updated_at_unix_ms,
           latest_plan_version,
           total_event_count,
           total_audit_entry_count,
           last_error_message,
           config_proto,
           has_latest_response,
           latest_response_proto
      FROM workflow_records
     WHERE workflow_id = ?
  )sql");
  statement.bind(1, std::string(workflow_id));
  if (!statement.executeStep()) {
    return std::nullopt;
  }
  return read_workflow_row(statement);
}

std::vector<protocol::WorkflowRecord> SqliteWorkflowStore::list_all_workflows() const {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    SELECT workflow_id,
           state,
           created_at_unix_ms,
           updated_at_unix_ms,
           latest_plan_version,
           total_event_count,
           total_audit_entry_count,
           last_error_message,
           config_proto,
           has_latest_response,
           latest_response_proto
      FROM workflow_records
     ORDER BY updated_at_unix_ms DESC, workflow_id ASC
  )sql");

  std::vector<protocol::WorkflowRecord> records;
  while (statement.executeStep()) {
    records.push_back(read_workflow_row(statement));
  }
  return records;
}

protocol::ListWorkflowsResponse SqliteWorkflowStore::list_workflows(
    const protocol::ListWorkflowsRequest& request) const {
  std::scoped_lock lock(mutex_);

  const std::size_t page_size = normalize_page_size(request.page_size());
  const std::size_t offset = parse_page_token(request.page_token());

  std::string sql =
      "SELECT workflow_id, state, created_at_unix_ms, updated_at_unix_ms, latest_plan_version, total_event_count, "
      "total_audit_entry_count, last_error_message FROM workflow_records";
  if (!request.states().empty()) {
    sql += " WHERE state IN (" + make_state_placeholders(request.states_size()) + ")";
  }
  sql += " ORDER BY updated_at_unix_ms DESC, workflow_id ASC LIMIT ? OFFSET ?";

  SQLite::Statement statement(*database_, sql);
  int bind_index = 1;
  bind_state_filters(statement, &bind_index, request.states());
  statement.bind(bind_index++, static_cast<std::int64_t>(page_size + 1));
  statement.bind(bind_index, static_cast<std::int64_t>(offset));

  protocol::ListWorkflowsResponse response;
  std::size_t count = 0;
  while (statement.executeStep()) {
    if (count == page_size) {
      response.set_next_page_token(std::to_string(offset + page_size));
      break;
    }
    *response.add_workflows() = read_summary_row(statement);
    ++count;
  }
  return response;
}

protocol::SearchWorkflowsResponse SqliteWorkflowStore::search_workflows(
    const protocol::SearchWorkflowsRequest& request) const {
  std::scoped_lock lock(mutex_);

  const std::size_t page_size = normalize_page_size(request.page_size());
  const std::size_t offset = parse_page_token(request.page_token());

  std::string sql =
      "SELECT workflow_id, state, created_at_unix_ms, updated_at_unix_ms, latest_plan_version, total_event_count, "
      "total_audit_entry_count, last_error_message FROM workflow_records";
  std::vector<std::string> clauses;
  if (!request.states().empty()) {
    clauses.push_back("state IN (" + make_state_placeholders(request.states_size()) + ")");
  }
  if (!request.query().empty()) {
    clauses.emplace_back("(instr(workflow_id, ?) > 0 OR instr(last_error_message, ?) > 0)");
  }
  if (!clauses.empty()) {
    sql += " WHERE ";
    for (std::size_t index = 0; index < clauses.size(); ++index) {
      if (index > 0) {
        sql += " AND ";
      }
      sql += clauses[index];
    }
  }
  sql += " ORDER BY updated_at_unix_ms DESC, workflow_id ASC LIMIT ? OFFSET ?";

  SQLite::Statement statement(*database_, sql);
  int bind_index = 1;
  bind_state_filters(statement, &bind_index, request.states());
  if (!request.query().empty()) {
    statement.bind(bind_index++, request.query());
    statement.bind(bind_index++, request.query());
  }
  statement.bind(bind_index++, static_cast<std::int64_t>(page_size + 1));
  statement.bind(bind_index, static_cast<std::int64_t>(offset));

  protocol::SearchWorkflowsResponse response;
  std::size_t count = 0;
  while (statement.executeStep()) {
    if (count == page_size) {
      response.set_next_page_token(std::to_string(offset + page_size));
      break;
    }
    *response.add_workflows() = read_summary_row(statement);
    ++count;
  }
  return response;
}

void SqliteWorkflowStore::write_event(std::string_view workflow_id, const protocol::WorkflowEventRecord& event_record) {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    INSERT OR REPLACE INTO workflow_events (
      workflow_id,
      sequence,
      recorded_at_unix_ms,
      boot_id,
      event_type,
      task_id,
      actor_id,
      detail,
      event_proto
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
  )sql");

  const std::string event_blob = serialize_proto(event_record.event());
  statement.bind(1, std::string(workflow_id));
  statement.bind(2, event_record.sequence());
  statement.bind(3, event_record.recorded_at_unix_ms());
  statement.bind(4, current_boot_id_);
  statement.bind(5, static_cast<int>(event_record.event().type()));
  statement.bind(6, event_record.event().task_id());
  statement.bind(7, event_record.event().actor_id());
  statement.bind(8, event_record.event().detail());
  bind_blob(statement, 9, event_blob);
  statement.exec();
}

std::vector<protocol::WorkflowEventRecord> SqliteWorkflowStore::list_events(std::string_view workflow_id,
                                                                            std::size_t limit) const {
  std::scoped_lock lock(mutex_);

  std::string sql =
      "SELECT sequence, recorded_at_unix_ms, event_proto FROM workflow_events WHERE workflow_id = ? ORDER BY sequence ";
  if (limit > 0) {
    sql += "DESC LIMIT ?";
  } else {
    sql += "ASC";
  }

  SQLite::Statement statement(*database_, sql);
  statement.bind(1, std::string(workflow_id));
  if (limit > 0) {
    statement.bind(2, static_cast<std::int64_t>(limit));
  }

  std::vector<protocol::WorkflowEventRecord> records;
  while (statement.executeStep()) {
    records.push_back(read_event_row(statement));
  }
  if (limit > 0) {
    std::ranges::reverse(records);
  }
  return records;
}

void SqliteWorkflowStore::write_plan_version(std::string_view workflow_id,
                                             const protocol::WorkflowPlanVersion& plan_version) {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    INSERT OR REPLACE INTO workflow_plan_versions (
      workflow_id,
      version,
      recorded_at_unix_ms,
      boot_id,
      response_proto
    ) VALUES (?, ?, ?, ?, ?)
  )sql");

  statement.bind(1, std::string(workflow_id));
  statement.bind(2, plan_version.version());
  statement.bind(3, plan_version.recorded_at_unix_ms());
  statement.bind(4, current_boot_id_);
  bind_blob(statement, 5, serialize_proto(plan_version.response()));
  statement.exec();
}

std::vector<protocol::WorkflowPlanVersion> SqliteWorkflowStore::list_plan_versions(std::string_view workflow_id,
                                                                                   std::size_t limit) const {
  std::scoped_lock lock(mutex_);

  std::string sql =
      "SELECT version, recorded_at_unix_ms, response_proto FROM workflow_plan_versions WHERE workflow_id = ? "
      "ORDER BY version ";
  if (limit > 0) {
    sql += "DESC LIMIT ?";
  } else {
    sql += "ASC";
  }

  SQLite::Statement statement(*database_, sql);
  statement.bind(1, std::string(workflow_id));
  if (limit > 0) {
    statement.bind(2, static_cast<std::int64_t>(limit));
  }

  std::vector<protocol::WorkflowPlanVersion> records;
  while (statement.executeStep()) {
    records.push_back(read_plan_row(statement));
  }
  if (limit > 0) {
    std::ranges::reverse(records);
  }
  return records;
}

void SqliteWorkflowStore::write_audit_entry(std::string_view workflow_id, const protocol::AuditEntry& audit_entry) {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    INSERT OR REPLACE INTO workflow_audit_entries (
      workflow_id,
      sequence,
      recorded_at_unix_ms,
      boot_id,
      actor,
      action,
      detail,
      audit_proto
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  )sql");

  statement.bind(1, std::string(workflow_id));
  statement.bind(2, audit_entry.sequence());
  statement.bind(3, audit_entry.recorded_at_unix_ms());
  statement.bind(4, current_boot_id_);
  statement.bind(5, audit_entry.actor());
  statement.bind(6, audit_entry.action());
  statement.bind(7, audit_entry.detail());
  bind_blob(statement, 8, serialize_proto(audit_entry));
  statement.exec();
}

std::vector<protocol::AuditEntry> SqliteWorkflowStore::list_audit_entries(std::string_view workflow_id,
                                                                          std::size_t limit) const {
  std::scoped_lock lock(mutex_);

  std::string sql =
      "SELECT sequence, recorded_at_unix_ms, actor, action, detail FROM workflow_audit_entries WHERE workflow_id = ? "
      "ORDER BY sequence ";
  if (limit > 0) {
    sql += "DESC LIMIT ?";
  } else {
    sql += "ASC";
  }

  SQLite::Statement statement(*database_, sql);
  statement.bind(1, std::string(workflow_id));
  if (limit > 0) {
    statement.bind(2, static_cast<std::int64_t>(limit));
  }

  std::vector<protocol::AuditEntry> records;
  while (statement.executeStep()) {
    records.push_back(read_audit_row(statement));
  }
  if (limit > 0) {
    std::ranges::reverse(records);
  }
  return records;
}

void SqliteWorkflowStore::put_idempotency_record(const protocol::IdempotencyRecord& record) {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    INSERT OR REPLACE INTO idempotency_records (
      key,
      workflow_id,
      request_fingerprint,
      recorded_at_unix_ms,
      boot_id,
      cached_response_proto
    ) VALUES (?, ?, ?, ?, ?, ?)
  )sql");

  statement.bind(1, record.key());
  statement.bind(2, record.workflow_id());
  statement.bind(3, record.request_fingerprint());
  statement.bind(4, record.recorded_at_unix_ms());
  statement.bind(5, current_boot_id_);
  bind_blob(statement, 6, serialize_proto(record.cached_response()));
  statement.exec();
}

std::optional<protocol::IdempotencyRecord> SqliteWorkflowStore::get_idempotency_record(std::string_view key) const {
  std::scoped_lock lock(mutex_);

  SQLite::Statement statement(*database_, R"sql(
    SELECT key, workflow_id, request_fingerprint, recorded_at_unix_ms, cached_response_proto
      FROM idempotency_records
     WHERE key = ?
  )sql");
  statement.bind(1, std::string(key));
  if (!statement.executeStep()) {
    return std::nullopt;
  }
  return read_idempotency_row(statement);
}

StorageMetrics SqliteWorkflowStore::get_storage_metrics(const std::int64_t recent_since_unix_ms) const {
  std::scoped_lock lock(mutex_);

  StorageMetrics metrics;

  SQLite::Statement recent_events_statement(*database_,
                                            "SELECT COUNT(*) FROM workflow_events WHERE recorded_at_unix_ms >= ?");
  recent_events_statement.bind(1, recent_since_unix_ms);
  if (recent_events_statement.executeStep()) {
    metrics.recent_event_count = recent_events_statement.getColumn(0).getInt64();
  }

  SQLite::Statement retained_plans_statement(*database_, "SELECT COUNT(*) FROM workflow_plan_versions");
  if (retained_plans_statement.executeStep()) {
    metrics.retained_plan_versions = retained_plans_statement.getColumn(0).getInt64();
  }

  SQLite::Statement tracked_workflows_statement(*database_, "SELECT COUNT(*) FROM workflow_records");
  if (tracked_workflows_statement.executeStep()) {
    metrics.tracked_workflows = tracked_workflows_statement.getColumn(0).getInt64();
  }

  SQLite::Statement active_workflows_statement(*database_,
                                               "SELECT COUNT(*) FROM workflow_records WHERE state IN (?, ?, ?, ?, ?)");
  active_workflows_statement.bind(1, static_cast<int>(protocol::pb::WORKFLOW_LIFECYCLE_STATE_SUBMITTED));
  active_workflows_statement.bind(2, static_cast<int>(protocol::pb::WORKFLOW_LIFECYCLE_STATE_PLANNING));
  active_workflows_statement.bind(3, static_cast<int>(protocol::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED));
  active_workflows_statement.bind(4, static_cast<int>(protocol::pb::WORKFLOW_LIFECYCLE_STATE_PAUSED));
  active_workflows_statement.bind(5, static_cast<int>(protocol::pb::WORKFLOW_LIFECYCLE_STATE_RECOVERING));
  if (active_workflows_statement.executeStep()) {
    metrics.active_workflows = active_workflows_statement.getColumn(0).getInt64();
  }

  return metrics;
}

StoragePruneResult SqliteWorkflowStore::prune_stale_boot_data(const std::int64_t recorded_before_unix_ms) {
  std::scoped_lock lock(mutex_);

  SQLite::Transaction transaction(*database_);
  StoragePruneResult result;

  SQLite::Statement prune_events(*database_, R"sql(
    DELETE FROM workflow_events
     WHERE boot_id <> ?
       AND recorded_at_unix_ms < ?
       AND sequence < COALESCE(
             (SELECT total_event_count
                FROM workflow_records
               WHERE workflow_records.workflow_id = workflow_events.workflow_id),
             sequence)
  )sql");
  prune_events.bind(1, current_boot_id_);
  prune_events.bind(2, recorded_before_unix_ms);
  result.pruned_event_rows = prune_events.exec();

  SQLite::Statement prune_plan_versions(*database_, R"sql(
    DELETE FROM workflow_plan_versions
     WHERE boot_id <> ?
       AND recorded_at_unix_ms < ?
       AND version < COALESCE(
             (SELECT latest_plan_version
                FROM workflow_records
               WHERE workflow_records.workflow_id = workflow_plan_versions.workflow_id),
             version)
  )sql");
  prune_plan_versions.bind(1, current_boot_id_);
  prune_plan_versions.bind(2, recorded_before_unix_ms);
  result.pruned_plan_version_rows = prune_plan_versions.exec();

  SQLite::Statement prune_audit_entries(*database_, R"sql(
    DELETE FROM workflow_audit_entries
     WHERE boot_id <> ?
       AND recorded_at_unix_ms < ?
       AND sequence < COALESCE(
             (SELECT total_audit_entry_count
                FROM workflow_records
               WHERE workflow_records.workflow_id = workflow_audit_entries.workflow_id),
             sequence)
  )sql");
  prune_audit_entries.bind(1, current_boot_id_);
  prune_audit_entries.bind(2, recorded_before_unix_ms);
  result.pruned_audit_entry_rows = prune_audit_entries.exec();

  SQLite::Statement prune_idempotency(*database_, R"sql(
    DELETE FROM idempotency_records
     WHERE boot_id <> ?
       AND recorded_at_unix_ms < ?
  )sql");
  prune_idempotency.bind(1, current_boot_id_);
  prune_idempotency.bind(2, recorded_before_unix_ms);
  result.pruned_idempotency_rows = prune_idempotency.exec();

  transaction.commit();

  if (result.pruned_event_rows > 0 || result.pruned_plan_version_rows > 0 || result.pruned_audit_entry_rows > 0 ||
      result.pruned_idempotency_rows > 0) {
    run_checkpoint(*database_);
  }
  database_->exec("PRAGMA optimize;");
  return result;
}

}  // namespace task_orchestrator::control_plane::store
