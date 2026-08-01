# High Level Design

This document outlines the high-level design of this project.
It provides an overview of the architecture, key components, and overall functionality.

## Architecture

The project is structured as follows:
- **Python Simulator**: Generates CDR records.
- **C++ Reducer/Digestor**: Processes and analyzes CDR records.
- **Database**: Stores and retrieves processed data.
- **API**: Provides access to the database.


### Config

Parses `config.toml` once at startup, validates it, exposes it as the global `cfg`.
Bad config throws.

### Logger

Level-filtered logging to stderr: timestamp, colored level tag, message.
Level comes from `config.toml`. Thread-safe.

### Python Simulator

The simulator generates CDR records to three different destinations:

1. **stdout (-p, --print)**: prints the records to the terminal
2. **file (-f, --file)**: writes the records to a file with an header (records count)
3. **rabbitmw (-r, --rabbit)**: sends the records to a RabbitMQ queue


