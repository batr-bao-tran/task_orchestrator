#include "runtime_service/in_memory_runtime_service.hpp"

#include <gtest/gtest.h>

#include <future>
#include <string>

#include "runtime_service/src/in_memory_runtime_service_detail.hpp"

namespace task_orchestrator::app {
namespace {
namespace to = task_orchestrator;
namespace tp = task_orchestrator::protocol;
namespace pb = task_orchestrator::protocol::pb;

tp::WorkflowEventStream stream_with_response() {
  const auto response = detail::make_runtime_error_response("stream error");
  co_yield detail::make_event(pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, "workflow_a", "accepted");
  co_yield detail::make_response_event(pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, "workflow_a", response, "done");
}

tp::WorkflowEventStream stream_without_response() {
  co_yield detail::make_event(pb::WORKFLOW_EVENT_TYPE_WORKFLOW_ACCEPTED, "workflow_b", "accepted");
}

TEST(InMemoryRuntimeServiceDetailTest, HelperConversionsPopulateApplicationWorkflowConfiguration) {
  auto ready_future = detail::make_ready_future(std::string("ready"));
  EXPECT_EQ("ready", ready_future.get());

  pb::ActorConfig actor;
  actor.set_id("robot_1");
  actor.set_type("robot");
  actor.set_capacity(3);
  actor.add_capabilities("lift");
  actor.add_capabilities("scan");
  actor.set_execution_cost_per_unit(1.5);
  pb::AvailabilityWindow* actor_window = actor.add_windows();
  actor_window->set_start(2);
  actor_window->set_end(9);

  const auto app_actor = detail::to_app_actor(actor);
  EXPECT_EQ("robot_1", app_actor.id);
  EXPECT_EQ("robot", app_actor.type);
  EXPECT_EQ(3, app_actor.capacity);
  EXPECT_EQ(2U, app_actor.capabilities.size());
  ASSERT_EQ(1U, app_actor.windows.size());
  EXPECT_EQ(2, app_actor.windows.front().start);
  EXPECT_EQ(9, app_actor.windows.front().end);
  EXPECT_DOUBLE_EQ(1.5, app_actor.execution_cost_per_unit);

  pb::TaskConfig task;
  task.set_id("pick");
  task.set_requested_time(4);
  task.set_duration(6);
  task.set_latest_start_time(5);
  task.set_deadline(11);
  task.set_priority(8);
  task.set_demand(2);
  task.set_mandatory(false);
  task.set_preemptible(true);
  task.add_allowed_actor_types("robot");
  task.add_allowed_actor_ids("robot_1");
  task.add_preferred_actor_ids("robot_1");
  task.add_required_capabilities("lift");
  task.add_dependency_task_ids("scan");
  task.add_mutually_exclusive_task_ids("charge");
  pb::ActorDistance* actor_distance = task.add_actor_distances();
  actor_distance->set_actor_id("robot_1");
  actor_distance->set_distance(7);
  task.set_tardiness_cost_per_unit(1.25);
  task.set_early_start_bonus(0.5);
  task.add_phase_durations(2);
  task.add_phase_durations(4);

  const auto app_task = detail::to_app_task(task);
  EXPECT_EQ("pick", app_task.id);
  EXPECT_EQ(4, app_task.requested_time);
  EXPECT_EQ(6, app_task.duration);
  EXPECT_EQ(5, app_task.latest_start_time);
  EXPECT_EQ(11, app_task.deadline);
  EXPECT_EQ(8, app_task.priority);
  EXPECT_EQ(2, app_task.demand);
  EXPECT_FALSE(app_task.mandatory);
  EXPECT_TRUE(app_task.preemptible);
  EXPECT_EQ(std::vector<std::string>({"robot"}), app_task.allowed_actor_types);
  EXPECT_EQ(std::vector<std::string>({"robot_1"}), app_task.allowed_actor_ids);
  EXPECT_EQ(std::vector<std::string>({"robot_1"}), app_task.preferred_actor_ids);
  EXPECT_EQ(std::vector<std::string>({"lift"}), app_task.required_capabilities);
  EXPECT_EQ(std::vector<std::string>({"scan"}), app_task.dependency_task_ids);
  EXPECT_EQ(std::vector<std::string>({"charge"}), app_task.mutually_exclusive_task_ids);
  EXPECT_EQ(7, app_task.actor_distances.at("robot_1"));
  EXPECT_DOUBLE_EQ(1.25, app_task.tardiness_cost_per_unit);
  EXPECT_DOUBLE_EQ(0.5, app_task.early_start_bonus);
  EXPECT_EQ(std::vector<to::Duration>({2, 4}), app_task.phase_durations);

  pb::ObjectiveConfig objective;
  objective.set_fulfilled_task_weight(500);
  objective.set_priority_weight(20);
  objective.set_makespan_weight(-2);
  objective.set_travel_distance_weight(-3);
  objective.set_tardiness_weight(-4);
  objective.set_execution_cost_weight(-5);
  objective.set_preferred_actor_weight(6);
  const auto app_objective = detail::to_app_objective(objective);
  EXPECT_EQ(500, app_objective.fulfilled_task_weight);
  EXPECT_EQ(20, app_objective.priority_weight);
  EXPECT_EQ(-2, app_objective.makespan_weight);
  EXPECT_EQ(-3, app_objective.travel_distance_weight);
  EXPECT_EQ(-4, app_objective.tardiness_weight);
  EXPECT_EQ(-5, app_objective.execution_cost_weight);
  EXPECT_EQ(6, app_objective.preferred_actor_weight);

  pb::WorkflowConfig workflow;
  workflow.set_id("workflow_demo");
  workflow.mutable_optimization()->set_backend("indexed_exact");
  workflow.mutable_optimization()->set_time_limit_ms(250);
  workflow.mutable_optimization()->set_relative_gap_limit(0.1);
  workflow.mutable_optimization()->set_num_search_workers(0);
  workflow.mutable_optimization()->set_allow_partial_plan(false);
  workflow.mutable_optimization()->mutable_objective()->CopyFrom(objective);
  workflow.add_actors()->CopyFrom(actor);
  workflow.add_tasks()->CopyFrom(task);

  const auto app_workflow = detail::to_app_workflow(workflow);
  EXPECT_EQ("workflow_demo", app_workflow.id);
  EXPECT_EQ("indexed_exact", app_workflow.optimization.backend);
  EXPECT_EQ(250, app_workflow.optimization.time_limit_ms);
  EXPECT_DOUBLE_EQ(0.1, app_workflow.optimization.relative_gap_limit);
  EXPECT_EQ(1, app_workflow.optimization.num_search_workers);
  EXPECT_FALSE(app_workflow.optimization.allow_partial_plan);
  ASSERT_EQ(1U, app_workflow.actors.size());
  ASSERT_EQ(1U, app_workflow.tasks.size());
  EXPECT_EQ("robot_1", app_workflow.actors.front().id);
  EXPECT_EQ("pick", app_workflow.tasks.front().id);
}

TEST(InMemoryRuntimeServiceDetailTest, ResponseAndEventHelpersProduceDeterministicProtocolMessages) {
  const to::app::WorkflowConfig workflow = [] {
    to::app::WorkflowConfig value{
        .id = "workflow_demo",
        .optimization = {},
        .actors = {},
        .tasks = {},
    };
    value.tasks.push_back(to::app::TaskConfig{
        .id = "pick",
        .requested_time = 0,
        .duration = 5,
        .latest_start_time = 0,
        .deadline = 0,
        .priority = 0,
        .demand = 1,
        .mandatory = true,
        .preemptible = false,
        .allowed_actor_types = {},
        .allowed_actor_ids = {},
        .preferred_actor_ids = {},
        .required_capabilities = {},
        .dependency_task_ids = {},
        .mutually_exclusive_task_ids = {},
        .actor_distances = {},
        .tardiness_cost_per_unit = 0.0,
        .early_start_bonus = 0.0,
        .phase_durations = {},
    });
    value.tasks.push_back(to::app::TaskConfig{
        .id = "pack",
        .requested_time = 0,
        .duration = 3,
        .latest_start_time = 0,
        .deadline = 0,
        .priority = 0,
        .demand = 1,
        .mandatory = true,
        .preemptible = false,
        .allowed_actor_types = {},
        .allowed_actor_ids = {},
        .preferred_actor_ids = {},
        .required_capabilities = {},
        .dependency_task_ids = {},
        .mutually_exclusive_task_ids = {},
        .actor_distances = {},
        .tardiness_cost_per_unit = 0.0,
        .early_start_bonus = 0.0,
        .phase_durations = {},
    });
    return value;
  }();
  const to::app::RunResult run_result{
      .ok = true,
      .capacity_issue = false,
      .assignments =
          {
              {.task_id = "pick", .actor_id = "robot_1", .start_time = 2},
          },
      .unfulfilled_task_ids = {"pack"},
      .error_message = {},
  };

  pb::RunResult proto_result;
  detail::populate_proto_run_result(run_result, workflow, &proto_result);
  ASSERT_TRUE(proto_result.ok());
  ASSERT_EQ(1, proto_result.assignments_size());
  EXPECT_EQ("pick", proto_result.assignments(0).task_id());
  EXPECT_EQ(7, proto_result.assignments(0).end_time());
  ASSERT_EQ(1, proto_result.unfulfilled_task_ids_size());
  EXPECT_EQ("pack", proto_result.unfulfilled_task_ids(0));

  const auto runtime_response = detail::make_runtime_response(run_result, workflow);
  EXPECT_TRUE(runtime_response.ok());
  EXPECT_EQ(1, runtime_response.result().assignments_size());

  const auto runtime_error = detail::make_runtime_error_response("runtime failure");
  EXPECT_FALSE(runtime_error.ok());
  EXPECT_EQ("runtime failure", runtime_error.error_message());
  EXPECT_EQ("runtime failure", runtime_error.result().error_message());

  const auto event = detail::make_event(pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED, "workflow_demo", "planning");
  EXPECT_EQ(pb::WORKFLOW_EVENT_TYPE_REPLANNING_STARTED, event.type());
  EXPECT_EQ("workflow_demo", event.workflow_id());
  EXPECT_EQ("planning", event.detail());

  const auto response_event =
      detail::make_response_event(pb::WORKFLOW_EVENT_TYPE_RUN_FINISHED, "workflow_demo", runtime_error, "finished");
  EXPECT_TRUE(response_event.has_response());
  EXPECT_EQ("finished", response_event.detail());
  EXPECT_FALSE(response_event.response().ok());

  EXPECT_EQ(5, detail::task_duration_for_id(workflow, "pick"));
  EXPECT_EQ(0, detail::task_duration_for_id(workflow, "missing"));

  const auto consumed_response = detail::consume_final_response(stream_with_response());
  EXPECT_FALSE(consumed_response.ok());
  EXPECT_EQ("stream error", consumed_response.error_message());

  const auto missing_response = detail::consume_final_response(stream_without_response());
  EXPECT_FALSE(missing_response.ok());
  EXPECT_NE(missing_response.error_message().find("did not produce a final response"), std::string::npos);
}

TEST(InMemoryRuntimeServiceDetailTest, AsyncSubmissionAndReorchestrationReturnCompletedResponses) {
  InMemoryWorkflowRuntimeService runtime_service;

  tp::SubmitWorkflowRequest submit_request;
  submit_request.mutable_config()->set_id("async_workflow");
  submit_request.mutable_config()->mutable_optimization()->set_backend("indexed_exact");
  auto* actor = submit_request.mutable_config()->add_actors();
  actor->set_id("robot_1");
  actor->set_type("robot");
  actor->set_capacity(1);
  auto* window = actor->add_windows();
  window->set_start(0);
  window->set_end(50);
  auto* task = submit_request.mutable_config()->add_tasks();
  task->set_id("pick");
  task->set_requested_time(0);
  task->set_duration(5);
  task->set_deadline(10);

  const tp::RuntimeApiResponse submit_response = runtime_service.submit_workflow_async(submit_request).get();
  ASSERT_TRUE(submit_response.ok());
  ASSERT_TRUE(submit_response.result().ok());
  ASSERT_EQ(1, submit_response.result().assignments_size());
  EXPECT_EQ("pick", submit_response.result().assignments(0).task_id());

  tp::ReorchestrateRequest reorchestrate_request;
  reorchestrate_request.set_workflow_id("async_workflow");
  reorchestrate_request.set_trigger_reorchestration(true);

  const tp::RuntimeApiResponse reorchestrate_response =
      runtime_service.reorchestrate_async(reorchestrate_request).get();
  ASSERT_TRUE(reorchestrate_response.ok());
  ASSERT_TRUE(reorchestrate_response.result().ok());
  ASSERT_EQ(1, reorchestrate_response.result().assignments_size());
  EXPECT_EQ("pick", reorchestrate_response.result().assignments(0).task_id());
}

}  // namespace
}  // namespace task_orchestrator::app
