# CTerminal

CTerminal is a lightweight, cross-platform terminal emulator that runs common Linux-style commands and a curated set of extra utilities on Windows, macOS and Linux. It aims to be simple, extensible, and friendly for scripting or interactive use.

## Features

* Core file and process commands: `ls`, `cd`, `pwd`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `ps`, `df`, `du`, `tree`, and more.
* Text tools: `grep`, `wc`, `head`, `tail` (including `tail -f`), `sort`, `uniq`, `replace`.
* Shell conveniences: aliases, history (including `history -c`), bookmarks, `which`, `open`, `edit` (uses `$EDITOR` or fallbacks).
* Utilities: `calc`, `random`, `ping`, `hash` (SHA-256 wrapper), `compress`/`extract` wrappers, `uptime`, `top`/`htop` wrapper, `net`, and desktop `notify` (where available).
* Cross-platform best-effort behavior: uses native APIs where practical and falls back to system utilities otherwise.
* Mint-inspired, configurable color scheme focused on readable, balanced output.

## Usage

Run the binary and enter commands at the prompt. Unknown commands are forwarded to the host shell. Aliases and bookmarks are session-local; consider persisting them if desired.

## Design goals & roadmap

* Keep the core small and readable while enabling easy addition of pure-C++ implementations (e.g., embedded SHA-256, zip handling).
* Add persistent config (aliases/bookmarks), readline-style editing and tab completion, and optional plugin support.
* Maintain a balanced, themeable UI (current look inspired by Linux Mint).

Contributions, ideas and patches are welcome.
