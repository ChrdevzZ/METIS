# Coding agent entry points

[AGENTS.md](../AGENTS.md) is the canonical, tool-neutral repository instruction
file. Keep active guidance concise: project scope, working commands, invariants,
validation and review rules. Architectural detail lives in
[architecture](architecture.md); options and defaults live in [building](building.md).
Do not duplicate those policies in vendor-specific adapters.

| Client | Repository entry |
| --- | --- |
| Codex | Native AGENTS.md discovery; no local config override is required |
| Claude Code | CLAUDE.md imports AGENTS.md using an unquoted @ import |
| Gemini CLI | GEMINI.md imports AGENTS.md using a relative @ import |
| GitHub Copilot | AGENTS.md on supporting agent surfaces; support varies by feature/client |
| Other agents | Use native AGENTS.md support, or explicitly read it before editing |

These are documented file conventions, not a universal instruction-loading
standard or an enforcement mechanism. No hooks, permissions, model settings,
credentials, global configuration or CI workflows are installed. The small
regular-file adapters avoid Windows symlink privileges. They import only the
same repository's AGENTS.md; the canonical file does not import adapters.

Each METIS/GKlib repository owns its own complete guidance. When working from
METIS into its GKlib submodule, explicitly read GKlib's AGENTS.md: automatic
nested discovery depends on the client, working directory and repository boundary.
A standalone GKlib checkout needs no METIS file. No task-specific model names,
local SDK paths or transient validation results belong in shared instructions.

## Checking discovery

Start a new Codex session at the intended repository root and ask it to list the
instruction files it loaded. In Claude Code inspect `/context`; in Gemini CLI
use `/memory reload` and `/memory show`. Repeat from a standalone GKlib checkout
and when accessing it as a submodule. For Copilot check the chosen feature's
instruction support rather than assuming a Markdown link automatically imports
another file. These interactive client checks are separate from static
repository validation and are not part of a normal build.

Static validation can check exact import targets, relative links, absence of
cycles, case-sensitive filenames and instruction size. It cannot prove a
particular installed client has loaded the instructions; record that separately.

## Official references

- [OpenAI: AGENTS.md discovery and review rules](https://learn.chatgpt.com/docs/agent-configuration/agents-md)
- [Claude Code: AGENTS.md imports](https://code.claude.com/docs/en/memory#agentsmd)
- [Gemini CLI: context file imports](https://geminicli.com/docs/cli/gemini-md/)
- [GitHub Copilot: repository instructions](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/add-custom-instructions/add-repository-instructions)
