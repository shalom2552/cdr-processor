---
name: module-checklist
description: Tests, reviews, logs, and documents a C++ module in the cdr-processor repo the way this project expects. Use when finishing a module (parser, source, aggregator, util), when asked to write tests for a class, to add or fix log lines, or to add a component to docs/DL_Design.md and docs/HL_Design.md.
---

# Module checklist

One request covers all six steps — a module is done when it is tested, reviewed, logged,
and written up. Copy this into the first reply and tick items off as they land:

```
- [ ] 1. Read the header, not the implementation
- [ ] 2. Write tests, run `make test`
- [ ] 3. Review the module, propose fixes, get a green suite
- [ ] 4. Set log levels
- [ ] 5. Add the component to both design docs
- [ ] 6. Report
```

In order, no stopping early. Only the user skips a step. A step that proves impossible
gets named in the report; the rest still happen.

## 1. Read the header, not the implementation

The `.hpp` gives the API. Reading the `.cpp` first makes the tests mirror its bugs.

Match one sibling for style: `tests/parser/pipe_parser.cpp`, `src/util/mapped_file.cpp`.

## 2. Tests

`src/x/y.cpp` gets `tests/x/y.cpp`. The Makefile finds it.

- doctest, snake_case names that read as sentences:
  `file_source_skips_bad_lines_and_keeps_the_rest`.
- Public API only.
- **Every wait bounded** — a broken module fails the suite, never hangs it.
- Cover: happy path, empty input, missing input, boundary sizes, use through the
  interface, `CHECK_FALSE(std::is_copy_constructible<T>::value)` when copies are deleted.
- No log-silencing helper; `tests/main.cpp` sets the level to `None` for the whole suite.
  A helper that changes the level restores the level it found, never the config one.

Run `make test`, report what came back, touch nothing yet.

## 3. Review and fix

Read the `.cpp` now. Small review, not an audit:

- a failing test — test wrong, or module wrong?
- lifetime: a reference or pointer outliving what it points at
- error paths: failure returning what success returns, so callers cannot tell
- hot path: per-record or per-line work that could happen once

One line per finding, worst first, cost stated as it is today. **Ask before changing
anything.** Rerun `make test` after approved fixes — green before step 4, and never
"done" on red.

## 4. Logs

| Level | Meaning |
|-------|---------|
| Error | work stops |
| Warn  | one item skipped, process continues |
| Info  | once per run or per file |
| Debug | per line or per batch |

Every message opens `[ComponentName]`. Move wrong-level lines, don't add beside them.

Never log in `src/config.cpp` — `Logger`'s constructor reads `cfg`, so logging during
`Config::load` re-enters `Config::instance()` mid-construction. Config errors stay throws.

## 5. Design docs

`docs/DL_Design.md`: one `##` per component, `###` per piece of a subsystem
(`## Source` → `### File Source`). File paths first, then a short paragraph on what it
does and what it costs.

`docs/HL_Design.md`: 2-4 lines under the same name.

Plain words. No how-to mechanics, no Notes or Gotchas sections, no defaults tables. Only
what the code does today — intent goes to the user, not the doc.

## 6. Report

One scan, no reading. Ten lines at most: a one-line verdict, then numbered items of one
line each, file paths included.

- No preamble, recap, summary section, or closing offer.
- No prose paragraphs. Needing a sentence of explanation means the change is too big.
- Numbers, not adjectives: `65/65 pass`.
- Only failing output gets quoted.
- Skipped work gets the last line.

## House rules

- Minimal diffs. No new file unless nothing existing fits — say so and let the user pick.
  No unasked abstraction.
- Edits, not python or sed rewrite scripts.
- Constructor and destructor bodies on their own lines, never one-liners.
- `/* ... */` above types and helpers; no narration inside function bodies.
- Ask before restructuring folders, adding config keys, or changing a public header.
- Commits: Conventional Commits, terse subject, body only when the why is not obvious.
