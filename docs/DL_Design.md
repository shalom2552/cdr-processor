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

`PipeParser` reads the generator format, 12 fields split on `|`:

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

## Thread Pool

`inc/thread_pool.hpp`, `src/thread_pool.cpp`.

Fixed workers, bounded queue. Built with a worker count and a queue size, both must be
above 0 or the constructor throws.

`submit()` takes a `std::function<void()>`, pushes it, and wakes one worker. When the
queue is full it blocks until a worker frees a slot, so a fast producer cannot outrun
the workers and grow memory without bound. It returns false only when the pool is
already stopping, and true once the task is queued.

One mutex guards the queue. Two condition variables sit on it: `m_cv_full` for
producers waiting for a slot, `m_cv_empty` for workers waiting for a task. Each worker
loops: wait for a task, pop it under the lock, drop the lock, run it. The task itself
runs outside the lock, so workers do not block each other.

An exception out of a task is caught in the worker loop and logged, and the worker
takes the next task. A worker never dies.

The destructor sets the stop flag, wakes everyone, and joins. Workers only leave the
loop once the queue is empty, so queued work is drained, not dropped. The worker vector
is declared last so the threads start after the rest of the state is live.

The pool is not copyable. It is safe to submit from many threads at once, and from
inside a running task.

Tests are in `tests/thread_pool.cpp`. They only use the public API and every wait is
bounded, so a broken pool fails instead of hanging the suite.
