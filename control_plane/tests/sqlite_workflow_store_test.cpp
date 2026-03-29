#include "control_plane/store/sqlite_workflow_store.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace {
namespace tcp = task_orchestrator::control_plane;
namespace tp = task_orchestrator::protocol;

std::filesystem::path make_temp_db(std::string_view suffix) {
  const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("task_orchestrator_sqlite_store_" + std::string(suffix) + "_" + std::to_string(unique));
  return directory / "control_plane.sqlite3";
}

tp::WorkflowRecord make_workflow_record(std::string_view workflow_id,
                                        tp::WorkflowLifecycleState state,
                                        std::int64_t updated_at_unix_ms,
                                        std::string_view last_error = {}) {
  tp::WorkflowRecord record;
  record.mutable_summary()->set_workflow_id(std::string(workflow_id));
  record.mutable_summary()->set_state(state);
  record.mutable_summary()->set_created_at_unix_ms(100);
  record.mutable_summary()->set_updated_at_unix_ms(updated_at_unix_ms);
  record.mutable_summary()->set_latest_plan_version(2);
  record.mutable_summary()->set_total_event_count(2);
  record.mutable_summary()->set_total_audit_entry_count(2);
  record.mutable_summary()->set_last_error_message(std::string(last_error));
  record.mutable_config()->set_id(std::string(workflow_id));
  return record;
}

tp::WorkflowEventRecord make_event_record(std::string_view workflow_id,
                                          std::int64_t sequence,
                                          std::int64_t recorded_at) {
  tp::WorkflowEventRecord event_record;
  event_record.set_sequence(sequence);
  event_record.set_recorded_at_unix_ms(recorded_at);
  event_record.mutable_event()->set_workflow_id(std::string(workflow_id));
  event_record.mutable_event()->set_type(tp::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED);
  event_record.mutable_event()->set_detail("accepted");
  return event_record;
}

tp::WorkflowPlanVersion make_plan_version(std::int64_t version, std::int64_t recorded_at, std::string_view actor_id) {
  tp::WorkflowPlanVersion plan_version;
  plan_version.set_version(version);
  plan_version.set_recorded_at_unix_ms(recorded_at);
  plan_version.mutable_response()->set_ok(true);
  plan_version.mutable_response()->mutable_result()->set_ok(true);
  auto* assignment = plan_version.mutable_response()->mutable_result()->add_assignments();
  assignment->set_task_id("pick");
  assignment->set_actor_id(std::string(actor_id));
  assignment->set_start_time(version);
  assignment->set_end_time(version + 5);
  return plan_version;
}

tp::AuditEntry make_audit_entry(std::int64_t sequence, std::int64_t recorded_at, std::string_view action) {
  tp::AuditEntry audit_entry;
  audit_entry.set_sequence(sequence);
  audit_entry.set_recorded_at_unix_ms(recorded_at);
  audit_entry.set_actor("tester");
  audit_entry.set_action(std::string(action));
  audit_entry.set_detail("Testing audit.");
  return audit_entry;
}

tp::IdempotencyRecord make_idempotency_record(std::string_view key, std::int64_t recorded_at) {
  tp::IdempotencyRecord idempotency_record;
  idempotency_record.set_key(std::string(key));
  idempotency_record.set_workflow_id("wf-1");
  idempotency_record.set_request_fingerprint("1234");
  idempotency_record.set_recorded_at_unix_ms(recorded_at);
  idempotency_record.mutable_cached_response()->set_ok(true);
  idempotency_record.mutable_cached_response()->mutable_result()->set_ok(true);
  return idempotency_record;
}

TEST(SqliteWorkflowStoreTest, PersistsWorkflowHistoryAndSupportsSummaryQueries) {
  const std::filesystem::path database_path = make_temp_db("persist");
  std::filesystem::remove_all(database_path.parent_path());

  tcp::store::SqliteWorkflowStore store(database_path, "boot-a");
  store.upsert_workflow(make_workflow_record("wf-1", tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED, 200));
  store.upsert_workflow(make_workflow_record(
      "wf-2", tp::pb::WORKFLOW_LIFECYCLE_STATE_FAILED, 300, "capacity issue in field-service-zone"));

  store.write_event("wf-1", make_event_record("wf-1", 1, 250));
  store.write_plan_version("wf-1", make_plan_version(2, 300, "robot_1"));
  store.write_audit_entry("wf-1", make_audit_entry(1, 350, "pause"));
  store.put_idempotency_record(make_idempotency_record("idem-1", 400));

  const auto workflow = store.get_workflow("wf-1");
  ASSERT_TRUE(workflow.has_value());
  EXPECT_EQ("wf-1", workflow->summary().workflow_id());
  EXPECT_EQ(2, workflow->summary().latest_plan_version());

  tp::ListWorkflowsRequest list_request;
  list_request.set_page_size(1);
  const auto list_response = store.list_workflows(list_request);
  ASSERT_EQ(1, list_response.workflows_size());
  EXPECT_EQ("wf-2", list_response.workflows(0).workflow_id());
  EXPECT_FALSE(list_response.next_page_token().empty());

  tp::SearchWorkflowsRequest search_request;
  search_request.set_query("capacity issue");
  const auto search_response = store.search_workflows(search_request);
  ASSERT_EQ(1, search_response.workflows_size());
  EXPECT_EQ("wf-2", search_response.workflows(0).workflow_id());

  tp::ListWorkflowsRequest filtered_request;
  filtered_request.add_states(tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED);
  const auto filtered_response = store.list_workflows(filtered_request);
  ASSERT_EQ(1, filtered_response.workflows_size());
  EXPECT_EQ("wf-1", filtered_response.workflows(0).workflow_id());

  const auto events = store.list_events("wf-1");
  ASSERT_EQ(1U, events.size());
  EXPECT_EQ(tp::pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, events.front().event().type());

  const auto plans = store.list_plan_versions("wf-1");
  ASSERT_EQ(1U, plans.size());
  EXPECT_EQ(2, plans.front().version());

  const auto audits = store.list_audit_entries("wf-1");
  ASSERT_EQ(1U, audits.size());
  EXPECT_EQ("pause", audits.front().action());

  const auto idempotency = store.get_idempotency_record("idem-1");
  ASSERT_TRUE(idempotency.has_value());
  EXPECT_EQ("wf-1", idempotency->workflow_id());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(SqliteWorkflowStoreTest, PrunesRowsFromStaleBootsButKeepsLatestWorkflowHistory) {
  const std::filesystem::path database_path = make_temp_db("prune");
  std::filesystem::remove_all(database_path.parent_path());

  {
    tcp::store::SqliteWorkflowStore old_store(database_path, "boot-old");
    old_store.upsert_workflow(make_workflow_record("wf-1", tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED, 500));
    old_store.write_event("wf-1", make_event_record("wf-1", 1, 100));
    old_store.write_event("wf-1", make_event_record("wf-1", 2, 200));
    old_store.write_plan_version("wf-1", make_plan_version(1, 100, "robot_1"));
    old_store.write_plan_version("wf-1", make_plan_version(2, 200, "robot_2"));
    old_store.write_audit_entry("wf-1", make_audit_entry(1, 100, "pause"));
    old_store.write_audit_entry("wf-1", make_audit_entry(2, 200, "resume"));
    old_store.put_idempotency_record(make_idempotency_record("idem-old", 100));
  }

  tcp::store::SqliteWorkflowStore new_store(database_path, "boot-new");
  const auto prune_result = new_store.prune_stale_boot_data(1'000);
  EXPECT_EQ(1, prune_result.pruned_event_rows);
  EXPECT_EQ(1, prune_result.pruned_plan_version_rows);
  EXPECT_EQ(1, prune_result.pruned_audit_entry_rows);
  EXPECT_EQ(1, prune_result.pruned_idempotency_rows);

  const auto events = new_store.list_events("wf-1");
  ASSERT_EQ(1U, events.size());
  EXPECT_EQ(2, events.front().sequence());

  const auto plans = new_store.list_plan_versions("wf-1");
  ASSERT_EQ(1U, plans.size());
  EXPECT_EQ(2, plans.front().version());

  const auto audits = new_store.list_audit_entries("wf-1");
  ASSERT_EQ(1U, audits.size());
  EXPECT_EQ(2, audits.front().sequence());

  EXPECT_FALSE(new_store.get_idempotency_record("idem-old").has_value());

  std::filesystem::remove_all(database_path.parent_path());
}

TEST(SqliteWorkflowStoreTest, UsesDefaultBootIdAndHandlesInvalidPaginationTokens) {
  const std::filesystem::path database_path = make_temp_db("pagination");
  std::filesystem::remove_all(database_path.parent_path());

  tcp::store::SqliteWorkflowStore store(database_path, "");
  store.upsert_workflow(
      make_workflow_record("wf-1", tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED, 300, "zone alpha unavailable"));
  store.upsert_workflow(
      make_workflow_record("wf-2", tp::pb::WORKFLOW_LIFECYCLE_STATE_FAILED, 200, "zone beta unavailable"));
  store.upsert_workflow(
      make_workflow_record("wf-3", tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED, 100, "zone gamma unavailable"));

  EXPECT_FALSE(store.get_workflow("missing").has_value());

  tp::ListWorkflowsRequest list_request;
  list_request.set_page_size(0);
  list_request.set_page_token("not-a-number");
  const auto list_response = store.list_workflows(list_request);
  ASSERT_EQ(3, list_response.workflows_size());
  EXPECT_EQ("wf-1", list_response.workflows(0).workflow_id());

  tp::SearchWorkflowsRequest search_request;
  search_request.add_states(tp::pb::WORKFLOW_LIFECYCLE_STATE_PLANNED);
  search_request.add_states(tp::pb::WORKFLOW_LIFECYCLE_STATE_FAILED);
  search_request.set_query("zone");
  search_request.set_page_size(1);
  search_request.set_page_token("bad-token");
  const auto search_response = store.search_workflows(search_request);
  ASSERT_EQ(1, search_response.workflows_size());
  EXPECT_EQ("wf-1", search_response.workflows(0).workflow_id());
  EXPECT_FALSE(search_response.next_page_token().empty());

  std::filesystem::remove_all(database_path.parent_path());
}

}  // namespace
