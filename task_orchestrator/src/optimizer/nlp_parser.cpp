#include "task_orchestrator/optimizer/nlp_parser.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace task_orchestrator::optimizer {
namespace {

constexpr Time kDefaultAvailabilityWindowStart = 0;
constexpr Time kDefaultAvailabilityWindowEnd = 1000000;
constexpr std::string_view kWorkflowPrefix = "workflow ";
constexpr std::string_view kIdPrefix = "id:";
constexpr std::string_view kActorsSection = "actors";
constexpr std::string_view kTasksSection = "tasks";
constexpr std::string_view kLineItemPrefix = "- ";

std::string trim(std::string value) {
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::ranges::find_if_not(value, is_space));
  value.erase(std::ranges::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
  return value;
}

std::vector<std::string> split(const std::string& input, char delimiter) {
  std::vector<std::string> parts;
  std::stringstream ss(input);
  std::string item;
  while (std::getline(ss, item, delimiter)) {
    item = trim(item);
    if (!item.empty()) {
      parts.push_back(item);
    }
  }
  return parts;
}

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> tokens;
  std::stringstream ss(line);
  std::string token;
  while (ss >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

std::vector<AvailabilityWindow> parse_windows(const std::string& spec) {
  std::vector<AvailabilityWindow> windows;
  for (const std::string& part : split(spec, ',')) {
    const auto dash = part.find('-');
    if (dash == std::string::npos) {
      continue;
    }
    windows.push_back({.start = std::stoll(part.substr(0, dash)), .end = std::stoll(part.substr(dash + 1))});
  }
  return windows;
}

std::unordered_map<ActorId, Time> parse_distances(const std::string& spec) {
  std::unordered_map<ActorId, Time> distances;
  for (const std::string& part : split(spec, ',')) {
    const auto eq = part.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    distances[trim(part.substr(0, eq))] = std::stoll(trim(part.substr(eq + 1)));
  }
  return distances;
}

bool is_section_line(const std::string& line, const std::string_view section) {
  return line == section || line == std::string(section) + ":";
}

std::optional<std::string> validate_model(const OptimizationModel& model) {
  std::unordered_set<ActorId> actor_ids;
  for (const OptimizerActor& actor : model.actors) {
    if (!actor_ids.insert(actor.id).second) {
      return "Duplicate actor id '" + actor.id + "' in controlled workflow request.";
    }
  }

  std::unordered_set<TaskId> task_ids;
  for (const OptimizerTask& task : model.tasks) {
    if (!task_ids.insert(task.id).second) {
      return "Duplicate task id '" + task.id + "' in controlled workflow request.";
    }
  }

  for (const OptimizerTask& task : model.tasks) {
    for (const TaskId& dependency_id : task.dependency_task_ids) {
      if (!task_ids.contains(dependency_id)) {
        return "Task '" + task.id + "' depends on unknown task '" + dependency_id + "'.";
      }
    }
    for (const TaskId& mutex_task_id : task.mutually_exclusive_task_ids) {
      if (!task_ids.contains(mutex_task_id)) {
        return "Task '" + task.id + "' declares unknown mutex task '" + mutex_task_id + "'.";
      }
    }
  }
  return std::nullopt;
}

std::string unknown_field_error(const std::string& entity_kind, const std::string& field, int line_number) {
  return "Unknown " + entity_kind + " field '" + field + "' on line " + std::to_string(line_number) + ".";
}

std::optional<std::string> parse_actor_line(const std::string& line, int line_number, OptimizerActor& actor) {
  const std::vector<std::string> tokens = tokenize(line);
  if (tokens.size() < 2 || tokens[0] != "actor") {
    return "Invalid actor declaration on line " + std::to_string(line_number) + ".";
  }

  actor = OptimizerActor{};
  actor.id = tokens[1];

  const std::unordered_set<std::string> allowed_fields = {"type", "capacity", "windows", "capabilities", "cost"};
  for (size_t i = 2; i < tokens.size(); ++i) {
    const std::string& field = tokens[i];
    if (!allowed_fields.contains(field)) {
      return unknown_field_error("actor", field, line_number);
    }
    if (i + 1 >= tokens.size()) {
      return "Missing value for actor field '" + field + "' on line " + std::to_string(line_number) + ".";
    }
    const std::string& value = tokens[++i];
    if (field == "type") {
      actor.type = value;
    } else if (field == "capacity") {
      actor.capacity = std::stoi(value);
    } else if (field == "windows") {
      actor.availability_windows = parse_windows(value);
    } else if (field == "capabilities") {
      actor.capabilities = split(value, ',');
    } else if (field == "cost") {
      actor.execution_cost_per_unit = std::stod(value);
    }
  }

  if (actor.id.empty()) {
    return "Actor id must not be empty on line " + std::to_string(line_number) + ".";
  }
  if (actor.type.empty()) {
    return "Actor type is required for actor '" + actor.id + "' on line " + std::to_string(line_number) + ".";
  }
  if (actor.availability_windows.empty()) {
    actor.availability_windows.push_back(
        {.start = kDefaultAvailabilityWindowStart, .end = kDefaultAvailabilityWindowEnd});
  }
  return std::nullopt;
}

std::optional<std::string> parse_task_line(const std::string& line, int line_number, OptimizerTask& task) {
  const std::vector<std::string> tokens = tokenize(line);
  if (tokens.size() < 2 || tokens[0] != "task") {
    return "Invalid task declaration on line " + std::to_string(line_number) + ".";
  }

  task = OptimizerTask{};
  task.id = tokens[1];

  const std::unordered_set<std::string> allowed_fields = {"duration",
                                                          "release",
                                                          "latest_start",
                                                          "deadline",
                                                          "priority",
                                                          "requires_type",
                                                          "requires_types",
                                                          "allowed_actor",
                                                          "allowed_actors",
                                                          "preferred_actor",
                                                          "preferred_actors",
                                                          "depends_on",
                                                          "depends",
                                                          "distances",
                                                          "distance",
                                                          "mandatory",
                                                          "preemptible",
                                                          "demand",
                                                          "requires_capabilities",
                                                          "mutex",
                                                          "tardiness_cost",
                                                          "early_start_bonus"};

  bool saw_duration = false;
  for (size_t i = 2; i < tokens.size(); ++i) {
    const std::string& field = tokens[i];
    if (!allowed_fields.contains(field)) {
      return unknown_field_error("task", field, line_number);
    }
    if (i + 1 >= tokens.size()) {
      return "Missing value for task field '" + field + "' on line " + std::to_string(line_number) + ".";
    }
    const std::string& value = tokens[++i];
    if (field == "duration") {
      task.duration = std::stoll(value);
      saw_duration = true;
    } else if (field == "release") {
      task.release_time = std::stoll(value);
    } else if (field == "latest_start") {
      task.latest_start_time = std::stoll(value);
    } else if (field == "deadline") {
      task.deadline = std::stoll(value);
    } else if (field == "priority") {
      task.priority = static_cast<Priority>(std::stoi(value));
    } else if (field == "requires_type" || field == "requires_types") {
      task.allowed_actor_types = split(value, ',');
    } else if (field == "allowed_actor" || field == "allowed_actors") {
      task.allowed_actor_ids = split(value, ',');
    } else if (field == "preferred_actor" || field == "preferred_actors") {
      task.preferred_actor_ids = split(value, ',');
    } else if (field == "depends_on" || field == "depends") {
      task.dependency_task_ids = split(value, ',');
    } else if (field == "distances" || field == "distance") {
      task.actor_distances = parse_distances(value);
    } else if (field == "mandatory") {
      task.mandatory = value == "true";
    } else if (field == "preemptible") {
      task.preemptible = value == "true";
    } else if (field == "demand") {
      task.demand = std::stoi(value);
    } else if (field == "requires_capabilities") {
      task.required_capabilities = split(value, ',');
    } else if (field == "mutex") {
      task.mutually_exclusive_task_ids = split(value, ',');
    } else if (field == "tardiness_cost") {
      task.tardiness_cost_per_unit = std::stod(value);
    } else if (field == "early_start_bonus") {
      task.early_start_bonus = std::stod(value);
    }
  }

  if (task.id.empty()) {
    return "Task id must not be empty on line " + std::to_string(line_number) + ".";
  }
  if (!saw_duration) {
    return "Task duration is required for task '" + task.id + "' on line " + std::to_string(line_number) + ".";
  }
  return std::nullopt;
}

}  // namespace

ParseResult parse_natural_language_request(std::string_view request) {
  ParseResult result;
  OptimizationModel model;
  model.id = "workflow";

  enum class Section { None, Actors, Tasks };
  Section section = Section::None;

  std::stringstream ss{std::string(request)};
  std::string raw_line;
  int line_number = 0;
  while (std::getline(ss, raw_line)) {
    ++line_number;
    std::string line = trim(raw_line);
    if (line.empty() || line.starts_with("#")) {
      continue;
    }
    if (line.starts_with(kWorkflowPrefix)) {
      model.id = trim(line.substr(std::string(kWorkflowPrefix).size()));
      continue;
    }
    if (line.starts_with(kIdPrefix)) {
      model.id = trim(line.substr(std::string(kIdPrefix).size()));
      continue;
    }
    if (is_section_line(line, kActorsSection)) {
      section = Section::Actors;
      continue;
    }
    if (is_section_line(line, kTasksSection)) {
      section = Section::Tasks;
      continue;
    }
    if (line.starts_with(kLineItemPrefix)) {
      line = trim(line.substr(std::string(kLineItemPrefix).size()));
    }

    if ((section == Section::Actors && line.starts_with("actor ")) || line.starts_with("actor ")) {
      OptimizerActor actor;
      if (const auto error = parse_actor_line(line, line_number, actor)) {
        result.error_message = *error;
        return result;
      }
      model.actors.push_back(std::move(actor));
      continue;
    }
    if ((section == Section::Tasks && line.starts_with("task ")) || line.starts_with("task ")) {
      OptimizerTask task;
      if (const auto error = parse_task_line(line, line_number, task)) {
        result.error_message = *error;
        return result;
      }
      model.tasks.push_back(std::move(task));
      continue;
    }

    result.error_message = "Unrecognized input on line " + std::to_string(line_number) +
                           ". The parser only supports the controlled workflow language.";
    return result;
  }

  if (model.actors.empty()) {
    result.error_message = "No actors were parsed from the natural-language request.";
    return result;
  }
  if (model.tasks.empty()) {
    result.error_message = "No tasks were parsed from the natural-language request.";
    return result;
  }
  if (const auto validation_error = validate_model(model)) {
    result.error_message = *validation_error;
    return result;
  }

  result.ok = true;
  result.model = std::move(model);
  return result;
}

}  // namespace task_orchestrator::optimizer
