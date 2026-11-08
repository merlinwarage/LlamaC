# LlamaC

**A native Win32 client for [llama-server](https://github.com/ggml-org/llama.cpp) written in pure C.** A single small `.exe`, no runtime, no Electron, no browser. It aims to feel like the llama.cpp WebUI. `Only 4MB of memory usage`.

![language](https://img.shields.io/badge/language-C-blue) ![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D6) ![deps](https://img.shields.io/badge/dependencies-none-brightgreen) ![version](https://img.shields.io/badge/version-2.0.0-lightgrey)

<img src="docs/main.png" alt="Console" width="49%"> <img src="docs/settings.png" alt="Config" width="49%">

## Features

- **Streaming chat** over llama-server's OpenAI-compatible API, with live token streaming and generation speed stats.
- **Tool calling.** Built-in file tools and MCP servers (stdio and HTTP), with a per-call approval dialog. See [Tools](#tools-file-access-and-mcp).
- **Long-term memory.** Facts the model saves are stored locally and injected into every chat. See [Memory](#long-term-memory).
- **Branching conversations.** Regenerate or edit a message and flip between alternatives (`‹ 1 / 2 ›`).
- **Reasoning support.** Collapsible "Thinking" blocks for `reasoning_content` and inline `<think>` tags.
- **Rich rendering** in a native RichEdit control: Markdown (headings, lists, quotes, tables, inline code), syntax-highlighted code blocks with **Copy** and **Download** buttons. Long code lines wrap inside the block.
- **Clickable links.** `[text](url)` links and bare `http(s)://` URLs open in your default browser. Only `http`, `https` and `mailto` links are opened.
- **Automatic context compression.** When a chat nears the context limit, older messages are summarized by the model so the conversation can go on. See [Context compression](#context-compression).
- **Attachments.** Images, audio (`.mp3`, `.wav`) and text files, via the button, drag and drop, or long paste that becomes an attachment.
- **Server-default aware settings.** Sampling parameters left empty are _not sent_, so llama-server's own values apply. Settings are read from `GET /props`.
- **Full sampling control:** temperature, top-k, top-p, min-p, repeat/presence/frequency penalty, DRY, max tokens, seed, stop strings.
- **Advanced:** Jinja `chat_template_kwargs`, `reasoning_format`, extra request JSON, and a **Jinja template viewer** (`POST /apply-template`).
- **WebUI-compatible import/export** of chats as JSON.
- **Search** across chat titles and contents.
- **Dark and light themes** with a Windows 11 style frame (DWM).
- **Portable.** Data is stored in a `data\` folder next to the exe. If that isn't writable, it falls back to `%APPDATA%\llamac`, then `%TEMP%\llamac`.

## Quick start

1. Start a llama.cpp server. Add `--jinja` if you want tools or memory, because the server needs it to parse tool calls:
    ```bat
    llama-server -m model.gguf --port 9931 --jinja
    ```
2. Run `llamac.exe`. Set the server URL, and optionally an API key, under **Settings**.

## Settings

Settings are grouped into sections: **Connection**, **Chat template**, **Display**, **Sampling**, **Tools & memory** and **Context & compression**. Hover any label or field for an explanation of what it does and what an empty value or `0` means.

Sampling fields left empty are not sent, so llama-server's own values apply. The grey hint in each box shows the server value. **Reset sampling** clears all sampling overrides.

## Tools: file access and MCP

Tools are sent to the model in the `tools` field of each chat request, so you don't need to mention them in the system prompt. The model decides when to call one. Every call shows an approval dialog (**Allow once**, **Always allow this tool (until restart)**, **Deny**) unless the tool is read-only or auto-approved. Up to **Max tool rounds** calls are chained per message.

Turn everything on or off with **Enable tools** in Settings. The model must support tool calling and the server must run with `--jinja`.

### File tools

| Tool               | Purpose                                       | Asks first |
| ------------------ | --------------------------------------------- | ---------- |
| `list_directory`   | List files and folders                        | no         |
| `read_file`        | Read a text file, optionally a line range     | no         |
| `search_files`     | Find files by wildcard and search their text  | no         |
| `grep`             | Regular-expression search in file contents    | no         |
| `glob`             | Find files by path pattern (`src/**/*.h`)     | no         |
| `write_file`       | Create, overwrite or append to a file         | yes        |
| `edit_file`        | Replace an exact piece of text (once, or all) | yes        |
| `create_directory` | Create a folder and missing parents           | yes        |
| `move_path`        | Move or rename a file or folder               | yes        |
| `delete_file`      | Delete one file                               | yes        |
| `run_command`      | Run a `cmd.exe` command, return its output    | yes        |

- The tools are limited to the **Root folder** set in Settings. Paths are resolved to their real location, so links and junctions cannot escape the root. With no root folder there are no file tools.
- Write, move and delete tools only exist when **Allow file write / edit tools** is ticked.
- `run_command` only exists when **Allow shell command tool** is ticked. It starts in the root folder, but the command itself can reach anything your Windows account can. It is not sandboxed.
- Tool results are capped at 60,000 characters.

### MCP servers

Click **Edit MCP servers...** in Settings to open the built-in `mcp.json` editor (JSON is validated on save). Click **Reload MCP** to connect, and the result appears in the status bar. Both transports are implemented with no third-party code.

```json
{
    "mcpServers": {
        "files": {
            "command": "npx",
            "args": ["-y", "@modelcontextprotocol/server-filesystem", "C:\MCP"],
            "env": {},
            "autoApprove": ["read_file"],
            "timeout": 60
        },
        "remote": {
            "url": "http://127.0.0.1:3000/mcp",
            "headers": { "Authorization": "Bearer TOKEN" }
        },
        "off": { "command": "x", "disabled": true }
    }
}
```

| Key           | Meaning                                                                   |
| ------------- | ------------------------------------------------------------------------- |
| `command`     | Start a stdio server (`.cmd` launchers such as `npx` are run through cmd) |
| `args`, `env` | Arguments, and extra environment variables                                |
| `cwd`         | Working directory for the child process                                   |
| `url`         | Use the Streamable HTTP transport (JSON and SSE replies)                  |
| `headers`     | Extra HTTP headers, for example authorization                             |
| `autoApprove` | `true`, or a list of tool names that run without asking                   |
| `timeout`     | Seconds per request (default 60)                                          |
| `disabled`    | Skip this server                                                          |

MCP tools appear to the model as `servername__toolname`. Stdio servers run inside a Windows job object, so the whole process tree is killed when llamac closes.

## Long-term memory

llamac keeps a small local memory so the model remembers you across chats.

- Facts are stored in `memory.json` in the data folder (up to 500; the oldest unpinned entry is dropped when full).
- A digest is added to the system message of every request, pinned facts first, then the newest. The model does not need to call a tool to recall them.
- Tools: `memory_save` (also updates duplicates), `memory_search`, `memory_list`, `memory_delete`. Save, search and list run without asking, and delete asks first.
- Toggle it with **Long-term memory** in Settings. You can review or edit `memory.json` in any editor.

## Context compression

With **Auto-compress when context is low** ticked, llamac keeps long chats within the server's context window.

- **When:** before each request, llamac estimates the prompt size (the last measured token count plus about 3 characters per token for anything added since, such as a large tool result). Compression starts when the estimate passes the threshold. It is also checked after every reply.
- **Threshold:** **Compress at %** of the context. `0` = auto, which keeps `max(15%, 4096 tokens)` free for the reply. Values are limited to 30-95%.
- **Context size:** the server's `n_ctx` from `/props`, or **Context limit** if you set one (for example to compress earlier than the server requires).
- **How:** the oldest ~70% of the messages are sent to the model with a request to summarize them. The summary replaces them at the top of the chat ("Summary of earlier messages") and is added to the system message of later requests. The original messages stay available: the previous branch (`‹`) on the summary shows them.
- **Waiting:** a message sent, or a tool round started, while compression runs waits for it, so the request is built from the compressed chat.
- **Reasoning models** think before they write the summary. Allow for that: with a 27B Qwen3 model, summarizing an 8k-token transcript took about 3 minutes. The summary request uses your **Template kwargs** and allows up to 6144 tokens.
- **If it fails,** the status bar shows the reason (server unreachable, timeout, HTTP error, empty reply). The same chat is not retried until it has grown by a few more messages.

A single tool result that is itself almost as large as the context cannot be compressed away, because the newest messages are always kept.

## Build

Requirements: Visual Studio 2022 (MSVC) on x64 Windows. No third-party libraries.

Open **x64 Native Tools Command Prompt for VS 2022** and run `build.bat`.

Link libraries are pulled in through `#pragma comment`. `build.bat` also embeds the application manifest (`/MANIFEST:EMBED`), which enables Common Controls v6. Without it, tooltips, grey hint text and themed controls do not appear. If you build by hand, keep those flags:

```bat
cl /O2 /W3 /utf-8 llamac.c /link /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
```

The version string is `LLAMAC_VERSION` at the top of `llamac.c`.

### File tools fail with "Win32 error 5"

If write_file reports `Win32 error 5` and the diagnostic shows `integrity=0x1000`,
llamac is running at Low integrity, usually because the exe or its folder has a Low
mandatory label (some sandboxing tools leave one behind). Check with
`icacls llamac.exe` and look for `Mandatory Label\Low Mandatory Level`, then
relabel the folder from an elevated prompt:

    icacls "<project folder>" /setintegritylevel "(OI)(CI)M" /T /C

Then rebuild.

## Keyboard shortcuts

| Shortcut       | Action         |
| -------------- | -------------- |
| `Ctrl+Shift+N` | New chat       |
| `Ctrl+Shift+D` | Delete chat    |
| `Ctrl+K`       | Search chats   |
| `Ctrl+B`       | Toggle sidebar |

## Project layout

The project is a unity build: `llamac.c` includes the modules in `src/` in dependency order, so everything compiles as one translation unit. **Order matters**: a function must be defined before it is used, otherwise MSVC assumes an `int` return type and reports redefinition errors.

```text
llamac.c            entry point, includes all modules
build.bat
src/
  util.h            string builder, allocation helpers
  json.h            tiny JSON DOM parser
  config.h          settings struct, field table, serialisation
  chat_tree.h       conversation tree, attachments
  tools_core.h      JSON dump, streamed tool_calls accumulator, UTF-8 sanitiser
  request.h         request body and /apply-template builders
  chat_io.h         WebUI-compatible import/export
  win_base.h        Windows includes, control IDs, global state
  win_util.h        UTF-8/16, file I/O, base64
  theme.h           colours, DWM frame, owner-drawn controls
  richtext.h        RichEdit output primitives
  highlight.h       syntax highlighter
  codeblocks.h      code blocks, Copy/Download links
  markdown.h        Markdown renderer
  render.h          thinking blocks, streaming, render_all
  storage.h         data directory, persistence
  conv_list.h       sidebar list and search
  attachments.h     attachment loading
  http.h            WinHTTP helpers
  worker.h          streaming worker thread
  text_window.h     read-only text window
  actions.h         send, regenerate, edit, delete, branch
  tools.h           tool registry, approval dialog, executor, runner thread
  memory.h          long-term memory store, tools, system-prompt digest
  mcp.h             MCP client core, mcp.json loading, built-in editor
  mcp_stdio.h       MCP over stdio (pipes, job object)
  mcp_http.h        MCP over Streamable HTTP (WinHTTP, SSE)
  tools_fs.h        sandboxed file tools
  settings_ui.h     settings dialog
  main_window.h     window procedure, layout, WinMain
  compact.h         chat compression
```

The portable modules (`util`, `json`, `config`, `chat_tree`, `tools_core`, `request`) use no Windows APIs.

## How it talks to the server

| Endpoint                    | Purpose                                                                                 |
| --------------------------- | --------------------------------------------------------------------------------------- |
| `POST /v1/chat/completions` | Streaming chat (`stream: true`, `timings_per_token: true`); non-streaming for summaries |
| `GET /props`                | Reads server default sampling values and the context size                               |
| `POST /apply-template`      | Renders the chat template without running inference                                     |

Only parameters you set in the client are included in the request. When tools are enabled, the request also carries the `tools` array, and tool results are sent back as `tool` messages.

## Security notes

- Tool calls are executed with your Windows account's permissions. Keep approval prompts on for write tools and for MCP servers you do not fully trust.
- MCP servers are programs you start. Only add servers you trust.
- `Auto-approve ALL tool calls` skips every prompt, including writes, deletes and shell commands. Combined with the shell tool, the model can run any command without asking.
- Links in model output are only opened when you click them, and only `http`, `https` and `mailto` links.

## Limitations

- Windows only (Win32, WinHTTP, RichEdit, DWM).
- Tools need a model with tool-calling support and `llama-server --jinja`.
- MCP support covers tools only (no resources, prompts or sampling), and server-initiated requests other than `ping` are declined.
- PDF attachments are not supported. Convert them to text or images first.
- Attachments are limited to 100 MB per file.
- Markdown rendering is a lightweight subset, not a full CommonMark implementation.

## Contributing

Issues and pull requests are welcome. Keep the code dependency-free and make sure it builds cleanly with `/W3`.

## License

MIT
