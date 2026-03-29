#include "runner/application.hpp"

int main(int argc, char** argv) { return task_orchestrator::app::Application::run_from_args(argc, argv); }
