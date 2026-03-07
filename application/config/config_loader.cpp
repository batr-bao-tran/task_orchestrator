#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <fstream>
#include <sstream>

#include "config/config.hpp"

namespace task_orchestrator::app {

namespace {

WorkflowConfig parse_yaml(const YAML::Node& root) {
  WorkflowConfig cfg;
  if (!root.IsMap()) return cfg;

  if (root["id"]) {
    cfg.id = root["id"].as<std::string>();
  }
  if (cfg.id.empty()) {
    cfg.id = "workflow";
  }

  if (root["actors"] && root["actors"].IsSequence()) {
    for (const auto& a : root["actors"]) {
      ActorConfig actor_config;
      if (a["id"]) {
        actor_config.id = a["id"].as<std::string>();
      }
      if (a["type"]) {
        actor_config.type = a["type"].as<std::string>();
      }
      if (a["capacity"]) {
        actor_config.capacity = a["capacity"].as<int>();
      }
      if (a["windows"] && a["windows"].IsSequence()) {
        for (const auto& w : a["windows"]) {
          AvailabilityWindowConfig availability_window_config;
          if (w["start"]) {
            availability_window_config.start = static_cast<Time>(w["start"].as<int64_t>());
          }
          if (w["end"]) {
            availability_window_config.end = static_cast<Time>(w["end"].as<int64_t>());
          }
          actor_config.windows.push_back(availability_window_config);
        }
      }
      if (actor_config.windows.empty()) {
        actor_config.windows.push_back({.start = 0, .end = 1000000});
      }
      cfg.actors.push_back(std::move(actor_config));
    }
  }

  if (root["tasks"] && root["tasks"].IsSequence()) {
    for (const auto& t : root["tasks"]) {
      TaskConfig task_config;
      if (t["id"]) {
        task_config.id = t["id"].as<std::string>();
      }
      if (t["requested_time"]) {
        task_config.requested_time = static_cast<Time>(t["requested_time"].as<int64_t>());
      }
      if (t["duration"]) {
        task_config.duration = static_cast<Duration>(t["duration"].as<int64_t>());
      }
      if (t["deadline"]) {
        task_config.deadline = static_cast<Time>(t["deadline"].as<int64_t>());
      }
      if (t["allowed_actor_types"] && t["allowed_actor_types"].IsSequence()) {
        for (const auto& ty : t["allowed_actor_types"]) {
          task_config.allowed_actor_types.push_back(ty.as<std::string>());
        }
      }
      if (task_config.deadline == 0) {
        task_config.deadline = task_config.requested_time + task_config.duration + 1000;
      }
      cfg.tasks.push_back(std::move(task_config));
    }
  }

  return cfg;
}

}  // namespace

WorkflowConfig load_config_from_string(const std::string& content) {
  try {
    YAML::Node root = YAML::Load(content);
    return parse_yaml(root);
  } catch (const YAML::Exception&) {
    return {};
  }
}

WorkflowConfig load_config_from_file(const char* path) {
  try {
    YAML::Node root = YAML::LoadFile(path);
    return parse_yaml(root);
  } catch (const YAML::Exception&) {
    return {};
  }
}

}  // namespace task_orchestrator::app
