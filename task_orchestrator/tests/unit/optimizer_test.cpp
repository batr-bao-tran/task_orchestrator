#include "task_orchestrator/optimizer/optimizer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "task_orchestrator/optimizer/discrete_time_formulation.hpp"

namespace {
namespace too = task_orchestrator::optimizer;

class DummyBackend final : public too::OptimizationBackend {
 public:
  explicit DummyBackend(std::string backend_name) : backend_name_(std::move(backend_name)) {}

  too::OptimizationSolution solve(const too::OptimizationModel&,
                                  const too::ConstraintIndex&,
                                  const too::OptimizationOptions&) const override {
    return too::OptimizationSolution{.ok = true,
                                     .assignments = {},
                                     .unfulfilled_task_ids = {},
                                     .backend_name = backend_name_,
                                     .stats = {},
                                     .error_message = {}};
  }

  const char* name() const override { return backend_name_.c_str(); }

 private:
  std::string backend_name_;
};

struct ParseSuccessCase {
  std::string name;
  std::string request;
  size_t expected_actor_count = 0;
  size_t expected_task_count = 0;
  std::string expected_last_task_dependency;
};

class OptimizerParserSuccessParamTest : public ::testing::TestWithParam<ParseSuccessCase> {};

TEST_P(OptimizerParserSuccessParamTest, ParsesControlledLanguageRequests) {
  const ParseSuccessCase& test_case = GetParam();
  const too::WorkflowOptimizer optimizer;
  const too::ParseResult parsed = task_orchestrator::optimizer::WorkflowOptimizer::parse_text(test_case.request);

  ASSERT_TRUE(parsed.ok) << parsed.error_message;
  EXPECT_EQ(test_case.expected_actor_count, parsed.model.actors.size());
  EXPECT_EQ(test_case.expected_task_count, parsed.model.tasks.size());
  ASSERT_FALSE(parsed.model.tasks.empty());
  if (!test_case.expected_last_task_dependency.empty()) {
    ASSERT_FALSE(parsed.model.tasks.back().dependency_task_ids.empty());
    EXPECT_EQ(test_case.expected_last_task_dependency, parsed.model.tasks.back().dependency_task_ids.front());
  }
}

INSTANTIATE_TEST_SUITE_P(ControlledLanguage,
                         OptimizerParserSuccessParamTest,
                         ::testing::Values(
                             ParseSuccessCase{
                                 .name = "basic_sections",
                                 .request = R"(
workflow warehouse_nlp
actors:
- actor r1 type robot capacity 1 windows 0-100
- actor m1 type machine capacity 1 windows 0-100
tasks:
- task scan duration 5 release 0 deadline 20 priority 10 requires_type robot preferred_actor r1
- task pack duration 10 release 0 deadline 40 priority 5 requires_type machine depends_on scan
)",
                                 .expected_actor_count = 2,
                                 .expected_task_count = 2,
                                 .expected_last_task_dependency = "scan",
                             },
                             ParseSuccessCase{
                                 .name = "comments_and_capabilities",
                                 .request = R"(
# controlled language only
workflow capability_demo
actors:
- actor tech1 type technician capacity 1 capabilities forklift,scanner
tasks:
- task inspect duration 7 release 0 requires_capabilities scanner mandatory true
)",
                                 .expected_actor_count = 1,
                                 .expected_task_count = 1,
                                 .expected_last_task_dependency = "",
                             },
                             ParseSuccessCase{
                                 .name = "id_prefix_and_extended_fields",
                                 .request = R"(
id: controlled_id
actors:
- actor robot_a type robot capacity 2 cost 1.5
tasks:
- task pick duration 3 release 1 latest_start 4 deadline 8 priority 9 requires_type robot allowed_actor robot_a preferred_actor robot_a distances robot_a=4 mandatory false preemptible true demand 2 tardiness_cost 1.25 early_start_bonus 0.5
)",
                                 .expected_actor_count = 1,
                                 .expected_task_count = 1,
                                 .expected_last_task_dependency = "",
                             },
                             ParseSuccessCase{
                                 .name = "ignores_invalid_window_and_distance_segments",
                                 .request = R"(
workflow ignore_invalid_segments
actors:
- actor robot_a type robot windows bad,0-10
tasks:
- task pick duration 1 distances bad,robot_a=2
)",
                                 .expected_actor_count = 1,
                                 .expected_task_count = 1,
                                 .expected_last_task_dependency = "",
                             }),
                         [](const ::testing::TestParamInfo<ParseSuccessCase>& info) { return info.param.name; });

struct ParseFailureCase {
  std::string name;
  std::string request;
  std::string expected_error_substring;
};

class OptimizerParserFailureParamTest : public ::testing::TestWithParam<ParseFailureCase> {};

TEST_P(OptimizerParserFailureParamTest, RejectsUnsupportedOrAmbiguousInput) {
  const ParseFailureCase& test_case = GetParam();
  const too::WorkflowOptimizer optimizer;
  const too::ParseResult parsed = task_orchestrator::optimizer::WorkflowOptimizer::parse_text(test_case.request);

  EXPECT_FALSE(parsed.ok);
  EXPECT_NE(std::string::npos, parsed.error_message.find(test_case.expected_error_substring));
}

INSTANTIATE_TEST_SUITE_P(ParserFailures,
                         OptimizerParserFailureParamTest,
                         ::testing::Values(
                             ParseFailureCase{
                                 .name = "freeform_prompt_rejected",
                                 .request = "Please schedule two robots and three packing tasks for tomorrow.",
                                 .expected_error_substring = "controlled workflow language",
                             },
                             ParseFailureCase{
                                 .name = "unknown_task_field",
                                 .request = R"(
workflow bad_field
actors:
- actor r1 type robot capacity 1
tasks:
- task pack duration 5 whimsical_value 42
)",
                                 .expected_error_substring = "Unknown task field",
                             },
                             ParseFailureCase{
                                 .name = "missing_duration",
                                 .request = R"(
workflow missing_duration
actors:
- actor r1 type robot capacity 1
tasks:
- task pack priority 4
)",
                                 .expected_error_substring = "Task duration is required",
                             },
                             ParseFailureCase{
                                 .name = "unknown_dependency",
                                 .request = R"(
workflow unknown_dep
actors:
- actor r1 type robot capacity 1
tasks:
- task pack duration 5 depends_on pick
)",
                                 .expected_error_substring = "depends on unknown task",
                             },
                             ParseFailureCase{
                                 .name = "duplicate_actor_id",
                                 .request = R"(
workflow dup_actor
actors:
- actor r1 type robot capacity 1
- actor r1 type robot capacity 1
tasks:
- task pack duration 5
)",
                                 .expected_error_substring = "Duplicate actor id",
                             },
                             ParseFailureCase{
                                 .name = "duplicate_task_id",
                                 .request = R"(
workflow dup_task
actors:
- actor r1 type robot capacity 1
tasks:
- task pack duration 5
- task pack duration 2
)",
                                 .expected_error_substring = "Duplicate task id",
                             },
                             ParseFailureCase{
                                 .name = "actor_missing_type",
                                 .request = R"(
workflow actor_missing_type
actors:
- actor r1 capacity 1
tasks:
- task pack duration 5
)",
                                 .expected_error_substring = "Actor type is required",
                             },
                             ParseFailureCase{
                                 .name = "actor_unknown_field",
                                 .request = R"(
workflow actor_unknown_field
actors:
- actor r1 type robot mysterious 1
tasks:
- task pack duration 5
)",
                                 .expected_error_substring = "Unknown actor field",
                             },
                             ParseFailureCase{
                                 .name = "actor_missing_field_value",
                                 .request = R"(
workflow actor_missing_value
actors:
- actor r1 type
tasks:
- task pack duration 5
)",
                                 .expected_error_substring = "Missing value for actor field",
                             },
                             ParseFailureCase{
                                 .name = "task_missing_field_value",
                                 .request = R"(
workflow missing_value
actors:
- actor r1 type robot capacity 1
tasks:
- task pack duration 5 priority
)",
                                 .expected_error_substring = "Missing value for task field",
                             },
                             ParseFailureCase{
                                 .name = "unknown_mutex_target",
                                 .request = R"(
workflow bad_mutex
actors:
- actor r1 type robot capacity 1
tasks:
- task pack duration 5 mutex missing
)",
                                 .expected_error_substring = "unknown mutex task",
                             },
                             ParseFailureCase{
                                 .name = "no_actors",
                                 .request = R"(
workflow no_actors
tasks:
- task pack duration 5
)",
                                 .expected_error_substring = "No actors",
                             },
                             ParseFailureCase{
                                 .name = "no_tasks",
                                 .request = R"(
workflow no_tasks
actors:
- actor r1 type robot capacity 1
)",
                                 .expected_error_substring = "No tasks",
                             }),
                         [](const ::testing::TestParamInfo<ParseFailureCase>& info) { return info.param.name; });

TEST(OptimizerTest, OptimizesParsedRequest) {
  const std::string request = R"(
workflow warehouse_nlp
actors:
- actor r1 type robot capacity 1 windows 0-100 capabilities scanner
- actor m1 type machine capacity 1 windows 0-100
tasks:
- task scan duration 5 release 0 deadline 20 priority 10 requires_capabilities scanner preferred_actor r1
- task pack duration 10 release 0 deadline 40 priority 5 requires_type machine depends_on scan
)";

  const too::WorkflowOptimizer optimizer;
  const too::OptimizationSolution solution = optimizer.optimize_text(request);
  ASSERT_TRUE(solution.ok);
  ASSERT_EQ(2U, solution.assignments.size());
  EXPECT_EQ("scan", solution.assignments[0].task_id);
  EXPECT_EQ("r1", solution.assignments[0].actor_id);
  EXPECT_EQ(0, solution.assignments[0].start_time);
  EXPECT_EQ("pack", solution.assignments[1].task_id);
  EXPECT_EQ("m1", solution.assignments[1].actor_id);
  EXPECT_EQ(5, solution.assignments[1].start_time);
  EXPECT_TRUE(solution.unfulfilled_task_ids.empty());
}

TEST(OptimizerTest, OptimizeTextReturnsParseErrorsWithoutThrowing) {
  const too::WorkflowOptimizer optimizer;

  const too::OptimizationSolution solution =
      optimizer.optimize_text("Please schedule two robots and three packing tasks for tomorrow.");

  EXPECT_FALSE(solution.ok);
  EXPECT_TRUE(solution.assignments.empty());
  EXPECT_NE(std::string::npos, solution.error_message.find("controlled workflow language"));
}

TEST(OptimizerTest, ExplicitBackendConstructorsUseProvidedBackendInstances) {
  const too::OptimizationModel empty_model;

  const too::WorkflowOptimizer backend_only_optimizer(std::make_unique<DummyBackend>("dummy_backend"));
  const too::OptimizationSolution backend_only_solution = backend_only_optimizer.optimize(empty_model);
  ASSERT_TRUE(backend_only_solution.ok);
  EXPECT_EQ("dummy_backend", backend_only_solution.backend_name);

  const too::WorkflowOptimizer backend_with_options_optimizer(std::make_unique<DummyBackend>("dummy_with_options"),
                                                              too::OptimizationOptions{
                                                                  .backend_kind = too::BackendKind::CommercialMip,
                                                                  .objective = {},
                                                              });
  const too::OptimizationSolution backend_with_options_solution = backend_with_options_optimizer.optimize(empty_model);
  ASSERT_TRUE(backend_with_options_solution.ok);
  EXPECT_EQ("dummy_with_options", backend_with_options_solution.backend_name);
}

TEST(OptimizerTest, MissingBackendProducesStructuredFailure) {
  const too::WorkflowOptimizer optimizer(too::OptimizationOptions{
      .backend_kind = too::BackendKind::OrToolsCpSat,
      .objective = {},
  });

  const too::OptimizationSolution solution = optimizer.optimize(too::OptimizationModel{});

  EXPECT_FALSE(solution.ok);
  EXPECT_EQ("ortools_cp_sat", solution.backend_name);
  EXPECT_NE(std::string::npos, solution.error_message.find("not linked"));
}

TEST(OptimizerTest, RegistryExposesIndexedBackendByDefault) {
  const auto backends = too::list_registered_backends();
  ASSERT_FALSE(backends.empty());
  EXPECT_TRUE(std::ranges::any_of(backends, [](const too::BackendDescriptor& descriptor) {
    return descriptor.kind == too::BackendKind::IndexedExact && descriptor.available;
  }));
}

TEST(OptimizerTest, SupportedBackendCatalogIncludesOptionalBackends) {
  const auto backends = too::list_supported_backends();
  ASSERT_EQ(3U, backends.size());
  EXPECT_TRUE(std::ranges::any_of(backends, [](const too::BackendDescriptor& descriptor) {
    return descriptor.kind == too::BackendKind::IndexedExact && descriptor.available && !descriptor.optional_dependency;
  }));
  EXPECT_TRUE(std::ranges::any_of(backends, [](const too::BackendDescriptor& descriptor) {
    return descriptor.kind == too::BackendKind::OrToolsCpSat && descriptor.optional_dependency;
  }));
  EXPECT_TRUE(std::ranges::any_of(backends, [](const too::BackendDescriptor& descriptor) {
    return descriptor.kind == too::BackendKind::CommercialMip && descriptor.optional_dependency;
  }));
}

TEST(OptimizerTest, BackendKindParsingAndFactoryFallbacksAreDeterministic) {
  EXPECT_EQ(too::BackendKind::IndexedExact, too::backend_kind_from_string("indexed_exact"));
  EXPECT_EQ(too::BackendKind::IndexedExact, too::backend_kind_from_string("indexed_branch_and_bound"));
  EXPECT_EQ(too::BackendKind::OrToolsCpSat, too::backend_kind_from_string("cp_sat"));
  EXPECT_EQ(too::BackendKind::OrToolsCpSat, too::backend_kind_from_string("ortools"));
  EXPECT_EQ(too::BackendKind::CommercialMip, too::backend_kind_from_string("mip"));
  EXPECT_EQ(too::BackendKind::Auto, too::backend_kind_from_string("unknown_backend"));

  EXPECT_EQ("indexed_exact", too::backend_kind_to_string(too::BackendKind::IndexedExact));
  EXPECT_EQ("ortools_cp_sat", too::backend_kind_to_string(too::BackendKind::OrToolsCpSat));
  EXPECT_EQ("commercial_mip", too::backend_kind_to_string(too::BackendKind::CommercialMip));
  EXPECT_EQ("auto", too::backend_kind_to_string(too::BackendKind::Auto));

  std::string error_message;
  std::unique_ptr<too::OptimizationBackend> backend = too::create_backend(too::BackendKind::Auto, &error_message);
  ASSERT_NE(nullptr, backend);
  EXPECT_EQ(std::string("indexed_branch_and_bound"), backend->name());
  EXPECT_TRUE(error_message.empty());

  backend = too::create_backend(too::BackendKind::OrToolsCpSat, &error_message);
  EXPECT_EQ(nullptr, backend);
  EXPECT_NE(std::string::npos, error_message.find("not linked"));
}

TEST(OptimizerTest, RegisterBackendReplacesExistingDescriptor) {
  too::register_backend(
      too::BackendDescriptor{
          .kind = too::BackendKind::CommercialMip,
          .name = "commercial_mip_stub",
          .available = false,
          .optional_dependency = true,
      },
      []() { return std::make_unique<DummyBackend>("stub"); });

  std::string error_message;
  std::unique_ptr<too::OptimizationBackend> backend =
      too::create_backend(too::BackendKind::CommercialMip, &error_message);
  EXPECT_EQ(nullptr, backend);
  EXPECT_NE(std::string::npos, error_message.find("registered but not available"));

  too::register_backend(
      too::BackendDescriptor{
          .kind = too::BackendKind::CommercialMip,
          .name = "commercial_mip_stub",
          .available = true,
          .optional_dependency = true,
      },
      []() { return std::make_unique<DummyBackend>("stub"); });

  backend = too::create_backend(too::BackendKind::CommercialMip, &error_message);
  ASSERT_NE(nullptr, backend);
  EXPECT_EQ(std::string("stub"), backend->name());

  too::register_backend(
      too::BackendDescriptor{
          .kind = too::BackendKind::CommercialMip,
          .name = "commercial_mip",
          .available = false,
          .optional_dependency = true,
      },
      []() { return std::make_unique<DummyBackend>("stub"); });
}

TEST(OptimizerTest, DiscreteTimeFormulationBuildsIndexedConstraints) {
  too::OptimizationModel model;
  model.id = "formulation_demo";
  model.actors.push_back(too::OptimizerActor{
      .id = "robot_1",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {"scanner"},
      .execution_cost_per_unit = 1.5,
  });
  model.actors.push_back(too::OptimizerActor{
      .id = "robot_2",
      .type = "robot",
      .capacity = 2,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {"scanner", "forklift"},
      .execution_cost_per_unit = 2.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "pick",
      .duration = 3,
      .release_time = 0,
      .latest_start_time = 4,
      .deadline = 8,
      .priority = 10,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {"robot_2"},
      .required_capabilities = {"scanner"},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {"audit"},
      .actor_distances = {{"robot_1", 5}, {"robot_2", 2}},
      .tardiness_cost_per_unit = 1.0,
      .early_start_bonus = 0.5,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "audit",
      .duration = 2,
      .release_time = 1,
      .latest_start_time = 6,
      .deadline = 9,
      .priority = 5,
      .demand = 1,
      .mandatory = false,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {"forklift"},
      .dependency_task_ids = {"pick"},
      .mutually_exclusive_task_ids = {"pick"},
      .actor_distances = {{"robot_2", 1}},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });

  const too::ConstraintIndex index(model);
  const too::OptimizationOptions options;
  const too::DiscreteTimeFormulation formulation = too::build_discrete_time_formulation(model, index, options);

  ASSERT_EQ(2U, formulation.task_decisions.size());
  EXPECT_EQ(1U, formulation.dependencies.size());
  EXPECT_EQ(1U, formulation.mutual_exclusions.size());
  EXPECT_FALSE(formulation.actor_time_slots.empty());
  EXPECT_GT(formulation.options.size(), 0U);
  EXPECT_EQ("pick", formulation.task_decisions[0].task_id);
  EXPECT_EQ("audit", formulation.task_decisions[1].task_id);
  EXPECT_TRUE(std::ranges::all_of(formulation.options, [](const too::DiscreteOption& option) {
    return option.actor_id != "robot_1" || option.task_id != "audit";
  }));
}

TEST(OptimizerTest, DiscreteTimeFormulationRespectsEligibilityAndDecodingBranches) {
  too::OptimizationModel model;
  model.id = "eligibility";
  model.actors.push_back(too::OptimizerActor{
      .id = "robot_1",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 5}},
      .capabilities = {"scanner"},
      .execution_cost_per_unit = 0.0,
  });
  model.actors.push_back(too::OptimizerActor{
      .id = "robot_2",
      .type = "robot",
      .capacity = 3,
      .availability_windows = {{.start = 2, .end = 8}},
      .capabilities = {"scanner", "lift"},
      .execution_cost_per_unit = 1.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "large_pick",
      .duration = 2,
      .release_time = 0,
      .latest_start_time = 5,
      .deadline = 7,
      .priority = 10,
      .demand = 2,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {"robot_2"},
      .required_capabilities = {"lift"},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "scan",
      .duration = 1,
      .release_time = 1,
      .latest_start_time = std::nullopt,
      .deadline = std::nullopt,
      .priority = 1,
      .demand = 1,
      .mandatory = false,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {"missing_actor", "robot_1", "robot_2"},
      .preferred_actor_ids = {},
      .required_capabilities = {"scanner"},
      .dependency_task_ids = {"large_pick"},
      .mutually_exclusive_task_ids = {"unknown_mutex"},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });

  too::OptimizationOptions options;
  options.allow_partial_plan = false;
  const too::ConstraintIndex index(model);
  const too::DiscreteTimeFormulation formulation = too::build_discrete_time_formulation(model, index, options);

  ASSERT_EQ(2U, formulation.task_decisions.size());
  EXPECT_TRUE(formulation.task_decisions[0].required);
  EXPECT_FALSE(formulation.task_decisions[1].required);
  EXPECT_FALSE(formulation.options.empty());
  EXPECT_TRUE(std::ranges::all_of(formulation.options, [](const too::DiscreteOption& option) {
    return option.task_id != "large_pick" || option.actor_id == "robot_2";
  }));
  EXPECT_TRUE(std::ranges::any_of(formulation.options, [](const too::DiscreteOption& option) {
    return option.task_id == "scan" && option.start_time >= 1;
  }));

  too::OptimizationSolution null_model_solution =
      too::decode_selected_options(too::DiscreteTimeFormulation{}, {0}, "decode_test");
  EXPECT_FALSE(null_model_solution.ok);
  EXPECT_NE(std::string::npos, null_model_solution.error_message.find("No optimization model"));

  too::OptimizationSolution decoded =
      too::decode_selected_options(formulation, {9999, formulation.options.back().option_index}, "decode_test");
  EXPECT_TRUE(decoded.ok);
  EXPECT_EQ("decode_test", decoded.backend_name);
  ASSERT_EQ(1U, decoded.assignments.size());
  EXPECT_EQ(formulation.options.back().task_id, decoded.assignments[0].task_id);
  EXPECT_EQ(1U, decoded.unfulfilled_task_ids.size());
}

TEST(OptimizerTest, ParsesExtendedActorAndTaskFields) {
  const too::WorkflowOptimizer optimizer;
  const too::ParseResult parsed = task_orchestrator::optimizer::WorkflowOptimizer::parse_text(R"(
id: extended_workflow
actors:
- actor r1 type robot capacity 2 capabilities scan,lift cost 1.25
tasks:
- task pack duration 4 release 2 latest_start 5 deadline 9 priority 7 requires_types robot allowed_actors r1 preferred_actors r1 depends scan distances r1=3 mandatory false preemptible true demand 2 requires_capabilities scan mutex audit tardiness_cost 1.5 early_start_bonus 0.5
- task scan duration 1 release 0 requires_type robot
- task audit duration 1 release 0 requires_type robot
)");

  ASSERT_TRUE(parsed.ok) << parsed.error_message;
  ASSERT_EQ(1U, parsed.model.actors.size());
  EXPECT_EQ("extended_workflow", parsed.model.id);
  EXPECT_EQ(2, parsed.model.actors[0].capacity);
  ASSERT_EQ(2U, parsed.model.actors[0].capabilities.size());
  EXPECT_DOUBLE_EQ(1.25, parsed.model.actors[0].execution_cost_per_unit);

  const too::OptimizerTask& task = parsed.model.tasks[0];
  EXPECT_EQ(4, task.duration);
  ASSERT_TRUE(task.latest_start_time.has_value());
  EXPECT_EQ(5, *task.latest_start_time);
  ASSERT_TRUE(task.deadline.has_value());
  EXPECT_EQ(9, *task.deadline);
  EXPECT_EQ(7, task.priority);
  EXPECT_EQ(2, task.demand);
  EXPECT_FALSE(task.mandatory);
  EXPECT_TRUE(task.preemptible);
  EXPECT_EQ(std::vector<std::string>({"robot"}), task.allowed_actor_types);
  EXPECT_EQ(std::vector<std::string>({"r1"}), task.allowed_actor_ids);
  EXPECT_EQ(std::vector<std::string>({"r1"}), task.preferred_actor_ids);
  EXPECT_EQ(std::vector<std::string>({"scan"}), task.dependency_task_ids);
  EXPECT_EQ(std::vector<std::string>({"scan"}), task.required_capabilities);
  EXPECT_EQ(std::vector<std::string>({"audit"}), task.mutually_exclusive_task_ids);
  EXPECT_EQ(3, task.actor_distances.at("r1"));
  EXPECT_DOUBLE_EQ(1.5, task.tardiness_cost_per_unit);
  EXPECT_DOUBLE_EQ(0.5, task.early_start_bonus);
}

TEST(OptimizerTest, DiscreteTimeBackendsRejectPreemptibleTasks) {
  too::OptimizationModel model;
  model.id = "preemptible";
  model.actors.push_back(too::OptimizerActor{
      .id = "robot_1",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "task_1",
      .duration = 3,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = 10,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = true,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });

  std::string error_message;
  EXPECT_FALSE(too::backend_model_supported(model, &error_message));
  EXPECT_NE(std::string::npos, error_message.find("Preemptible"));
}

TEST(OptimizerTest, DiscreteTimeBackendSupportsNonPreemptibleTasks) {
  too::OptimizationModel model;
  model.tasks.push_back(too::OptimizerTask{
      .id = "task_1",
      .duration = 1,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = std::nullopt,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });

  std::string error_message = "unexpected";
  EXPECT_TRUE(too::backend_model_supported(model, &error_message));
  EXPECT_EQ("unexpected", error_message);
}

TEST(OptimizerTest, DiscreteTimeFormulationSkipsInfeasibleWindowsAndSortsDecodedAssignments) {
  too::OptimizationModel model;
  model.id = "sort_decode";
  model.actors.push_back(too::OptimizerActor{
      .id = "too_short",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 0}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  });
  model.actors.push_back(too::OptimizerActor{
      .id = "release_blocked",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 5, .end = 5}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  });
  model.actors.push_back(too::OptimizerActor{
      .id = "feasible",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {},
      .execution_cost_per_unit = 0.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "later_task",
      .duration = 3,
      .release_time = 4,
      .latest_start_time = std::nullopt,
      .deadline = std::nullopt,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });
  model.tasks.push_back(too::OptimizerTask{
      .id = "earlier_task",
      .duration = 1,
      .release_time = 0,
      .latest_start_time = 0,
      .deadline = 2,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });

  const too::ConstraintIndex index(model);
  const too::DiscreteTimeFormulation formulation = too::build_discrete_time_formulation(model, index, {});

  ASSERT_EQ(2U, formulation.task_decisions.size());
  EXPECT_TRUE(std::ranges::all_of(formulation.options, [](const too::DiscreteOption& option) {
    return option.actor_id != "too_short" && option.actor_id != "release_blocked";
  }));

  const auto earlier_it = std::ranges::find_if(
      formulation.options, [](const too::DiscreteOption& option) { return option.task_id == "earlier_task"; });
  const auto later_it =
      std::ranges::find_if(formulation.options,

                           [](const too::DiscreteOption& option) { return option.task_id == "later_task"; });
  ASSERT_NE(formulation.options.end(), earlier_it);
  ASSERT_NE(formulation.options.end(), later_it);

  const too::OptimizationSolution decoded =
      too::decode_selected_options(formulation, {later_it->option_index, earlier_it->option_index}, "decode_sorted");

  ASSERT_TRUE(decoded.ok);
  ASSERT_EQ(2U, decoded.assignments.size());
  EXPECT_EQ("earlier_task", decoded.assignments.front().task_id);
  EXPECT_EQ("later_task", decoded.assignments.back().task_id);
}

TEST(OptimizerTest, DiscreteTimeFormulationSkipsActorsMissingFromTheModel) {
  too::OptimizationModel indexed_model;
  indexed_model.id = "indexed_model";
  indexed_model.actors.push_back(too::OptimizerActor{
      .id = "robot_1",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {"scan"},
      .execution_cost_per_unit = 0.0,
  });
  indexed_model.tasks.push_back(too::OptimizerTask{
      .id = "pick",
      .duration = 2,
      .release_time = 0,
      .latest_start_time = std::nullopt,
      .deadline = 6,
      .priority = 1,
      .demand = 1,
      .mandatory = true,
      .preemptible = false,
      .allowed_actor_types = {"robot"},
      .allowed_actor_ids = {},
      .preferred_actor_ids = {},
      .required_capabilities = {"scan"},
      .dependency_task_ids = {},
      .mutually_exclusive_task_ids = {},
      .actor_distances = {},
      .tardiness_cost_per_unit = 0.0,
      .early_start_bonus = 0.0,
  });

  too::OptimizationModel formulation_model = indexed_model;
  formulation_model.actors.clear();
  formulation_model.actors.push_back(too::OptimizerActor{
      .id = "robot_2",
      .type = "robot",
      .capacity = 1,
      .availability_windows = {{.start = 0, .end = 10}},
      .capabilities = {"scan"},
      .execution_cost_per_unit = 0.0,
  });

  const too::ConstraintIndex index(indexed_model);
  const too::DiscreteTimeFormulation formulation = too::build_discrete_time_formulation(formulation_model, index, {});

  ASSERT_EQ(1U, formulation.task_decisions.size());
  EXPECT_TRUE(formulation.task_decisions.front().option_indexes.empty());
  EXPECT_TRUE(formulation.options.empty());
}

TEST(OptimizerTest, AutoBackendFailsClearlyWhenAllBackendsAreUnavailable) {
  too::register_backend(
      too::BackendDescriptor{
          .kind = too::BackendKind::IndexedExact,
          .name = "indexed_exact",
          .available = false,
          .optional_dependency = false,
      },
      []() { return std::make_unique<DummyBackend>("indexed_unavailable"); });

  std::string error_message;
  std::unique_ptr<too::OptimizationBackend> backend = too::create_backend(too::OptimizationOptions{}, &error_message);
  EXPECT_EQ(nullptr, backend);
  EXPECT_NE(std::string::npos, error_message.find("No optimization backend"));
}

}  // namespace
