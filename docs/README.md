# CDR Processor

A distributed C++ system for processing telecom Charging Data Records.
Ingests multi-GB CDR files in parallel, aggregates them exposes the results
over an API.

## RoadMap

- [x] Phase 0 - Buildable repo skeleton & CI
- [ ] Phase 1 - Core primitives, parser, config
- [ ] Phase 2 - Stream & ingest CDR files
- [ ] Phase 3 - Subscriber, operator & graph aggregation
- [ ] Phase 4 - REST query gateway API
- [ ] Phase 5 - MySQL persistence across restarts
- [ ] Phase 6 - Distributed harvesters & clients
- [ ] Phase 7 - Profiling, tests & deliverables
