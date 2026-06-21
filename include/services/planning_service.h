#pragma once
#include "services/discovery_service.h"
#include <string>
#include <vector>

namespace Services {

struct TaskItem {
  std::string description;
  std::string file_ref; // empty if not file-specific
};

struct TaskPlan {
  std::vector<TaskItem> tasks;
  std::string query;
};

class PlanningService {
public:
  static TaskPlan generate_plan(const std::string &query,
                                 const ProjectDiscovery &discovery);

  // Returns selected indices (1-based). Empty means cancelled.
  static std::vector<int> prompt_approval(int task_count);

  // Serialize plan for AI context
  static std::string to_context_string(const TaskPlan &plan,
                                        const std::vector<int> &selected);
};

} // namespace Services
