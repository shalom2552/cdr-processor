# Detailed Level Design

## CDR Record

`inc/cdr_record.hpp`.

Plain struct, one record. No methods, no parsing — the reader fills it in.

`UsageType` has eight values: `MOC` and `MTC` for outgoing and incoming calls,
`SMS_MO` and `SMS_MT` for messages, `D` for data, and `U`, `B`, `X` for calls that
went unanswered, hit a busy line, or failed.

`callTime` is the date and time together as `std::time_t`, `duration` is in seconds.
The byte counts are only used for data. The second party fields are 0 when there is
no second party.

## Parser

`inc/iparser.hpp`, `inc/pipe_parser.hpp`, `src/pipe_parser.cpp`.

`IParser::parse()` takes a line and returns an empty optional when the line is bad.
Another format means another class behind the same interface, picked by `source.format`
in `config.toml`.

`PipeParser` reads the simulator format, 12 fields split on `|`:

```
seq|imsi|imei|usage|msisdn|DD/MM/YYYY|HH:MM:SS|duration|rx|tx|sp_imsi|sp_msisdn
```

Numbers go through `std::from_chars`, so no locale and no exceptions. The byte counts
and second party fields may be empty and read as 0. Date and time are parsed by hand
and joined with `timegm`, so the result is UTC.

A line that does not fit is logged at debug level and skipped.

## Config

`inc/config.hpp`, `src/config.cpp`.

Singleton, built on first use. `load()` reads `config.toml` with toml++, `validate()`
checks the values. Both are private, so nothing can change the config later.

The header defines `inline const Config& cfg`, so any file that includes it reads
settings as `cfg.rabbit.url`.

Bad values throw and stop the program at startup.

## Logger

`inc/logger.hpp`, `src/logger.cpp`.

Singleton. Levels are `Debug < Info < Warning < Error < None`. Anything below the active
level is dropped, and `None` drops everything. The level comes from `cfg.log.level`.

Each line looks like:

```
YYYY-MM-DD HH:MM:SS [LEVEL] message
```

The time is gray and the level tag is colored: gray debug, green info, yellow warn,
red error. Write logs with `logDebug`, `logInfo`, `logWarn`, `logError`.

Output goes to stderr. A mutex keeps lines from mixing when threads log at once.
