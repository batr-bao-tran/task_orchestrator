#include <yaml-cpp/yaml.h>

#include <cstdint>
#include <fstream>
#include <sstream>

#include "config/config.hpp"

namespace task_orchestrator::app {

namespace {

WorkflowConfig parse_yaml(YAML const ::Node& root) {
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
      ActorConfig ac;
      if (a["id"]) {
        ac.id = a["id"].as<std::string>();
      }
      if (a["type"]) {
        ac.type = a["type"].as<std::string>();
      }
      if (a["capacity"]) {
        ac.capacity = a["capacity"].as<int>();
      }
      if (a["windows"] && a["windows"].IsSequence()) {
        for (const auto& w : a["windows"]) {
          AvailabilityWindowConfig win;
          if (w["start"]) {
            win.start = static_cast<Time>(w["start"].as<int64_t>());
          }
          if (w["end"]) {
            win.end = static_cast<Time>(w["end"].as<int64_t>());
          }
          ac.windows.push_back(win);
        }
      }
      if (ac.windows.empty()) {
        ac.windows.push_back({0, 1000000});
      }
      cfg.actors.push_back(std::move(ac));
    }
  }

  if (root["tasks"] && root["tasks"].IsSequence()) {
    for (const auto& t : root["tasks"]) {
      TaskConfig tc;
      if (t["id"]) {
        tc.id = t["id"].as<std::string>();
      }
      if (t["requested_time"]) {
        tc.requested_time = static_cast<Time>(t["requested_time"].as<int64_t>());
      }
      if (t["duration"]) {
        tc.duration = static_cast<Duration>(t["duration"].as<int64_t>());
      }
      if (t["deadline"]) {
        tc.deadline = static_cast<Time>(t["deadline"].as<int64_t>());
      }
      if (t["allowed_actor_types"] && t["allowed_actor_types"].IsSequence()) {
        for (const auto& ty : t["allowed_actor_types"]) {
          tc.allowed_actor_types.push_back(ty.as<std::string>());
        }
      }
      if (tc.deadline == 0) {
        tc.deadline = tc.requested_time + tc.duration + 1000;
      }
      cfg.tasks.push_back(std::move(tc));
    }
  }

  return cfg;
}

}  // namespace

WorkflowConfig load_config_from_string(const std::string& content) {
  try {
    YAML::Node root = YAML::Load(content);
    return parse_yaml(root);
  } catch (YAML const ::Exception&) {
    return {};
  }
}

WorkflowConfig load_config_from_file(const char* path) {
  try {
    YAML::Node root = YAML::LoadFile(path);
    return parse_yaml(root);
  } catch (YAML const ::Exception&) {
    return {};
  }
}

}  // namespace task_orchestrator::app
