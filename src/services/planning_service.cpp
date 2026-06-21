#include "services/planning_service.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace Services {

namespace {

std::string to_lower(std::string s) {
  for (auto &c : s)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string extract_feature_name(const std::string &query) {
  std::string lower = to_lower(query);
  // Strip leading action verb
  std::vector<std::string> verbs = {"add ",      "implement ", "refactor ",
                                     "fix ",      "migrate ",   "create ",
                                     "remove ",   "update ",    "delete ",
                                     "rename ",   "extract ",   "upgrade ",
                                     "build ",    "install ",   "setup ",
                                     "configure "};
  for (auto &v : verbs) {
    if (lower.find(v) == 0) {
      std::string rest = query.substr(v.size());
      // Keep first 5 words max
      std::istringstream iss(rest);
      std::string word, result;
      int count = 0;
      while (iss >> word && count < 5) {
        if (!result.empty())
          result += " ";
        result += word;
        count++;
      }
      return result;
    }
  }
  return query;
}

// Map a filename to a task description based on its role
std::string task_for_file(const std::string &file,
                           const std::string &feature) {
  std::string lower = to_lower(file);

  if (lower.find("command_router") != std::string::npos ||
      lower.find("router") != std::string::npos)
    return "Add command routing for " + feature;
  if (lower.find("replay") != std::string::npos)
    return "Implement " + feature + " in replay service";
  if (lower.find("session") != std::string::npos)
    return "Update session handling for " + feature;
  if (lower.find("service") != std::string::npos)
    return "Update service layer for " + feature;
  if (lower.find("ui_manage") != std::string::npos ||
      lower.find("/ui/") != std::string::npos)
    return "Update UI for " + feature;
  if (lower.find("main") != std::string::npos)
    return "Update entry point for " + feature;
  if (lower.find("test") != std::string::npos)
    return "Add tests for " + feature;
  if (lower.find("config") != std::string::npos)
    return "Update configuration for " + feature;
  if (lower.find("auth") != std::string::npos)
    return "Update authentication for " + feature;
  if (lower.find("memory") != std::string::npos)
    return "Update memory handling for " + feature;

  return "Modify " + file + " for " + feature;
}

} // namespace

TaskPlan PlanningService::generate_plan(const std::string &query,
                                         const ProjectDiscovery &discovery) {
  TaskPlan plan;
  plan.query = query;
  std::string feature = extract_feature_name(query);

  // Generate tasks from relevant files
  for (auto &file : discovery.relevant_files) {
    // Only include files that are likely implementation files
    std::string lower = to_lower(file);
    if (lower.find(".h") != std::string::npos ||
        lower.find(".cpp") != std::string::npos) {
      plan.tasks.push_back({task_for_file(file, feature), file});
    }
  }

  // Cap at 6 tasks
  if (plan.tasks.size() > 6)
    plan.tasks.resize(6);

  // Add testing task if tests exist
  if (discovery.has_tests) {
    bool has_test_task = false;
    for (auto &t : plan.tasks) {
      if (t.description.find("test") != std::string::npos ||
          t.description.find("Test") != std::string::npos) {
        has_test_task = true;
        break;
      }
    }
    if (!has_test_task) {
      plan.tasks.push_back({"Add tests for " + feature, ""});
    }
  }

  // Always add verification task
  plan.tasks.push_back({"Verify build and existing tests pass", ""});

  return plan;
}

std::vector<int> PlanningService::prompt_approval(int task_count) {
  std::cout << "\nProceed? [y/N] (e.g. 1,2-4 for selection): " << std::flush;

  std::string line;
  std::getline(std::cin, line);

  line.erase(0, line.find_first_not_of(" \t"));
  line.erase(line.find_last_not_of(" \t") + 1);

  if (line.empty() || line == "n" || line == "N" || line == "no")
    return {};

  if (line == "y" || line == "Y" || line == "yes") {
    std::vector<int> all;
    for (int i = 1; i <= task_count; i++)
      all.push_back(i);
    return all;
  }

  std::vector<int> selected;
  std::istringstream iss(line);
  std::string part;
  while (std::getline(iss, part, ',')) {
    part.erase(0, part.find_first_not_of(" \t"));
    part.erase(part.find_last_not_of(" \t") + 1);
    size_t dash = part.find('-');
    if (dash != std::string::npos) {
      int start = std::stoi(part.substr(0, dash));
      int end = std::stoi(part.substr(dash + 1));
      for (int i = start; i <= end; i++)
        selected.push_back(i);
    } else {
      selected.push_back(std::stoi(part));
    }
  }

  return selected;
}

std::string
PlanningService::to_context_string(const TaskPlan &plan,
                                    const std::vector<int> &selected) {
  std::string result = "Task Plan:\n";
  for (auto idx : selected) {
    if (idx >= 1 && idx <= static_cast<int>(plan.tasks.size())) {
      auto &t = plan.tasks[idx - 1];
      result += "  " + t.description + "\n";
      if (!t.file_ref.empty())
        result += "    files: " + t.file_ref + "\n";
    }
  }
  return result;
}

} // namespace Services
