# Detailed Level Design

## Config

`inc/config.hpp`, `src/config.cpp`.

Singleton, built on first use. `load()` reads `config.toml` with toml++, `validate()`
checks the values. Both are private, so nothing can change the config later.

The header defines `inline const Config& cfg`, so any file that includes it reads
settings as `cfg.rabbit.url`.

Bad values throw and stop the program at startup.

## Logger

`inc/logger.hpp`, `src/logger.cpp`.

Singleton. Levels are `Debug < Info < Warning < Error`. Anything below the active level
is dropped. The level comes from `cfg.log.level`.

Each line looks like:

```
YYYY-MM-DD HH:MM:SS [LEVEL] message
```

The time is gray and the level tag is colored: gray debug, green info, yellow warn,
red error. Write logs with `logDebug`, `logInfo`, `logWarn`, `logError`.

Output goes to stderr. A mutex keeps lines from mixing when threads log at once.
