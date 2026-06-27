#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace Core {

bool is_tty_stream(FILE *stream);

// Simple menu -- returns index of chosen item, or -1 on escape.
int show_menu(const std::string &title,
              const std::vector<std::string> &items,
              int default_index);

// Menu with a preview panel rendered below the items.
// preview_fn(i) is called for the currently selected index and may return a
// multi-line string that is displayed while the cursor is on that item.
int show_menu(const std::string &title,
              const std::vector<std::string> &items,
              int default_index,
              std::function<std::string(int)> preview_fn);

} // namespace Core
