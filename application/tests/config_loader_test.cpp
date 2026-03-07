#include <gtest/gtest.h>

#include "config/config.hpp"

using namespace task_orchestrator::app;

TEST(ConfigLoaderTest, ParseYaml) {
  std::string content = R"(
id: w1
actors:
  - id: a1
    type: robot
    capacity: 2
    windows:
      - start: 0
        end: 100
  - id: a2
    type: machine
    capacity: 1
    windows:
      - start: 0
        end: 50
      - start: 60
        end: 100
tasks:
  - id: t1
    requested_time: 0
    duration: 10
    deadline: 20
    allowed_actor_types: [robot]
  - id: t2
    requested_time: 5
    duration: 15
    deadline: 30
    allowed_actor_types: [robot, machine]
)";
  WorkflowConfig cfg = load_config_from_string(content);
  EXPECT_EQ(cfg.id, "w1");
  ASSERT_EQ(cfg.actors.size(), 2u);
  EXPECT_EQ(cfg.actors[0].id, "a1");
  EXPECT_EQ(cfg.actors[0].type, "robot");
  EXPECT_EQ(cfg.actors[0].capacity, 2);
  ASSERT_EQ(cfg.actors[0].windows.size(), 1u);
  EXPECT_EQ(cfg.actors[0].windows[0].start, 0);
  EXPECT_EQ(cfg.actors[0].windows[0].end, 100);

  EXPECT_EQ(cfg.actors[1].id, "a2");
  ASSERT_EQ(cfg.actors[1].windows.size(), 2u);
  EXPECT_EQ(cfg.actors[1].windows[1].start, 60);
  EXPECT_EQ(cfg.actors[1].windows[1].end, 100);

  ASSERT_EQ(cfg.tasks.size(), 2u);
  EXPECT_EQ(cfg.tasks[0].id, "t1");
  EXPECT_EQ(cfg.tasks[0].requested_time, 0);
  EXPECT_EQ(cfg.tasks[0].duration, 10);
  EXPECT_EQ(cfg.tasks[0].deadline, 20);
  ASSERT_EQ(cfg.tasks[0].allowed_actor_types.size(), 1u);
  EXPECT_EQ(cfg.tasks[0].allowed_actor_types[0], "robot");

  EXPECT_EQ(cfg.tasks[1].allowed_actor_types.size(), 2u);
}
