#include "app/menu.h"

#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#else
#include <termios.h>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace Core {

bool is_tty_stream(FILE *stream) {
#ifdef _WIN32
  return _isatty(_fileno(stream)) != 0;
#else
  return isatty(fileno(stream)) != 0;
#endif
}

int show_menu(const std::string &title,
                     const std::vector<std::string> &items,
                     int default_index) {
  bool tty = is_tty_stream(stdin) && is_tty_stream(stdout);
  if (!tty || items.empty()) {
    return default_index;
  }

#ifdef _WIN32
  (void)title;
  return default_index;
#else
  int selected = std::clamp(default_index, 0, (int)items.size() - 1);

  std::cout << "\033[?25l";

  struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  std::cout << title << ":\n";
  for (size_t i = 0; i < items.size(); i++) {
    std::cout << (i == (size_t)selected ? "\xe2\x80\xba " : "  ") << items[i] << "\n";
  }

  while (true) {
    unsigned char raw;
    if (read(STDIN_FILENO, &raw, 1) != 1) {
      break;
    }
    int ch = raw;

    if (ch == '\n' || ch == '\r') {
      break;
    }

    if (ch == 0x1B) {
      struct timeval tv = {0, 50000};
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(STDIN_FILENO, &fds);

      bool is_arrow = false;
      char arrow = 0;
      if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        char seq[2] = {0};
        if (read(STDIN_FILENO, seq, 1) == 1 && seq[0] == '[') {
          if (read(STDIN_FILENO, seq + 1, 1) == 1) {
            is_arrow = true;
            arrow = seq[1];
          }
        }
      }

      if (is_arrow) {
        if (arrow == 'A' && selected > 0) {
          selected--;
        } else if (arrow == 'B' && selected < (int)items.size() - 1) {
          selected++;
        } else {
          continue;
        }
        std::cout << "\033[" << items.size() << "A";
        for (size_t i = 0; i < items.size(); i++) {
          std::cout << (i == (size_t)selected ? "\xe2\x80\xba " : "  ") << items[i] << "\033[K\n";
        }
      } else {
        selected = -1;
        break;
      }
    }
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

  // Clear menu
  std::cout << "\033[" << (items.size() + 1) << "A\033[J\033[?25h";

  return selected;
#endif
}

} // namespace Core
