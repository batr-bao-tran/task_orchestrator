#ifndef TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__CONCEPTS_HPP_
#define TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__CONCEPTS_HPP_
#include <concepts>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

#include "utils/types.hpp"

namespace task_orchestrator {

/** Type has an identifier (e.g. Phase, Process, Actor). */
template <typename T>
concept Identifiable = requires(const T& t) {
  { t.id } -> std::convertible_to<std::string>;
};

/** Type represents a schedulable task with duration and priority. */
template <typename T>
concept SchedulableTask = requires(const T& t) {
  { t.id } -> std::convertible_to<TaskId>;
  { t.estimated_duration } -> std::convertible_to<Duration>;
  { t.priority } -> std::convertible_to<Priority>;
};

/** Type has availability windows (e.g. Actor). */
template <typename T>
concept HasAvailability = requires(const T& t, Time tm, Duration d) {
  { t.availability_windows } -> std::ranges::range;
  { t.can_accept_at(tm, d) } -> std::same_as<bool>;
  { t.next_available_start(tm, d) } -> std::same_as<std::optional<Time>>;
};

/** Range of phase IDs. */
template <typename R>
concept PhaseIdRange = std::ranges::range<R> && std::same_as<std::ranges::range_value_t<R>, PhaseId>;

}  // namespace task_orchestrator

#endif  // TASK_ORCHESTRATOR__TASK_ORCHESTRATOR_INCLUDE_TASK_ORCHESTRATOR_CORE__CONCEPTS_HPP_
