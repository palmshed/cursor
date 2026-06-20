# Cursor Runtime Help

Use `/help` or `/docs` to display this document.

## Core Commands

```text
/help           Show help
/tools          Show available tools
/clear          Clear the screen
/exit           Exit Cursor
/quit           Exit Cursor
```

## Files

```text
@<path>                         Include file or directory content
/files <patterns>              Read multiple files
read:<file>                    Read a file
write:<file> <content>         Write a file
replace:<file>:<old>:<new>     Replace text in a file
grep:<pattern>                 Search text in files
tree:<path>                    Show directory tree
```

Example:

```text
@src/main.cpp Explain this code.
```

## Shell

```text
!<command>                     Run a shell command
!                              Toggle shell mode
cmd:<command>                  Execute a command safely
build:<command>                Run build commands
```

Examples:

```text
!git status
cmd:ls -la
build:cmake --build build
```

## Web

```text
search:<query>                 Search the web
/fetch <url> [format]          Fetch web content
```

Examples:

```text
search:c++20 modules
/fetch https://example.com
```

## Git

```text
git:status                     Repository status
git:log                        Commit history
git:analyze                    Repository analysis
```

## GitHub

```text
/github repo:owner/repo
/github issues:owner/repo
/github health:owner/repo
```

Examples:

```text
/github repo:bniladridas/cursor
/github issues:llvm/llvm-project
```

## Memory

```text
/memory show                   Show memory
/memory add <text>             Add memory
remember:<fact>                Save a fact
/compress                      Compress conversation context
```

## Sessions

```text
/chat save <tag>               Save session
/chat resume <tag>             Resume session
/chat list                     List saved sessions
```

## Tasks

```text
/goal show                     Show current goal
/goal clear                    Clear goal

/task add <description>        Add task
/task list                     List tasks
/task complete <id>            Complete task
/task remove <id>              Remove task

/params set key=value          Set parameters
/params show                   Show parameters
/params clear                  Clear parameters
```

## Advanced

```text
/debug                         Toggle diagnostics
/stats                         Session statistics

/context show                  Show context
/context refresh               Refresh context
/context create                Create CURSOR.md

/checkpoint <cmd>              Manage checkpoints
/restore [id]                  Restore checkpoint

/mcp <cmd>                     MCP management
/auth <cmd>                    Authentication
/theme <cmd>                   Themes
/sandbox <cmd>                 Sandbox management
/error <cmd>                   Error management
```

## Philosophy

Cursor is a terminal-native AI coding agent.

The primary interface is conversation:

```text
> explain this code

cursor

Here's what the function does...
```

Commands are available when needed, but should stay out of the way during normal use.
