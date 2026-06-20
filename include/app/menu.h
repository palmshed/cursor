#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace Core {

bool is_tty_stream(FILE *stream);
int show_menu(const std::string &title,
              const std::vector<std::string> &items,
              int default_index);

} // namespace Core
