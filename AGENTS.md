# Cursor Runtime Help

This file is the live runtime help source for the Cursor agent.

Use `/help` or `/docs` to display this content at runtime. If this file cannot be loaded,
the agent falls back to built-in help output.

## Available Meta Commands

- `/help` or `/?` - Show runtime help
- `/docs` - Show runtime help
- `/debug` - Toggle verbose/debug mode (shows all agent reasoning)
- `/tools` - Show available tools
- `/clear` - Clear screen
- `/chat save <tag>` - Save conversation state
- `/chat resume <tag>` - Resume conversation state
- `/chat list` - List saved conversations
- `/memory show` - Show memory context
- `/memory add <text>` - Add a fact to memory
- `/compress` - Compress conversation context
- `/stats` - Show session statistics
- `/context show` - Show hierarchical context
- `/context refresh` - Refresh context cache
- `/context create` - Create `CURSOR.md`
- `/files <patterns>` - Read multiple files via glob or path patterns
- `/fetch <url> [format]` - Fetch web content (text/json/raw)
- `/checkpoint <cmd>` - Manage checkpoints (create/list/delete)
- `/restore [id]` - List or restore checkpoints
- `/mcp <cmd>` - MCP server management
- `/theme <cmd>` - Theme management
- `/auth <cmd>` - Authentication management
- `/sandbox <cmd>` - Sandboxed execution management
- `/error <cmd>` - Error management and reporting
- `/goal show` - Show the current goal and task status
- `/goal clear` - Clear current goal, tasks, and params
- `/task add <description>` - Add a task for the current goal
- `/task list` - List active tasks
- `/task complete <id>` - Mark a task complete
- `/task remove <id>` - Remove a task
- `/params set key=value` - Set goal/task parameters
- `/params show` - Show current parameters
- `/params clear` - Clear current parameters- `/github repo:owner/repo` - Repository info
- `/github issues:owner/repo` - List repository issues
- `/github health:owner/repo` - Run repository health check
- `/quit` or `/exit` - Exit the program

## File Injection

- `@<path>` - Include file or directory content in the prompt
- Example: `@src/main.cpp What does this code do?`

## Shell Commands

- `!<command>` - Execute shell command
- `!` - Toggle shell mode
- `build:command` - Execute build or shell commands

## Direct Commands

- `cmd:<command>` - Execute shell command safely
- `read:<file>[:start:count]` - Read file contents
- `write:<file> <content>` - Write content to a file
- `replace:<file>:<old>:<new>[:expected_count]` - Replace text in a file
- `grep:<pattern>[:directory[:file_filter]]` - Search text in files
- `search:<query>` - Search the web
- `remember:<fact>` - Save a fact to memory
- `analyze:<path>` - Analyze project structure
- `components:<path>` - Find main components
- `todos:<path>` - Find task comments
- `tree:<path>` - Show directory tree
- `git:log` - Show git history
- `git:status` - Show git status
- `git:analyze` - Analyze git repository

## Runtime Help Integration

This file is the single live runtime help document for cursor.

Do not add an additional runtime help file.
