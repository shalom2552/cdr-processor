#pragma once

#include <cstddef>
#include <string_view>
#include <cstdint>

inline constexpr std::string_view kConfigPath = "config.toml";

// CDR record
inline constexpr std::size_t kFieldCount = 12;      // number of fields in a CDR record
inline constexpr std::size_t kMaxImsiDigits = 15;   // maximum digits in IMSI
inline constexpr std::size_t kMaxMsisdnDigits = 15; // maximum digits in MSISDN

// source
inline constexpr std::size_t kBatchSize = 4096;

// file source
inline constexpr std::size_t kFileBatchSize = kBatchSize;

// dir watcher
inline constexpr std::size_t kBacklogAlert = 16; // queued files before the backlog is logged

// rabbit source
static constexpr int kPollMs = 100;
static constexpr size_t kRabbitBatchSize = kBatchSize;
inline constexpr std::size_t kRabbitPrefetch  = 2 * kRabbitBatchSize;
static_assert(kRabbitPrefetch <= UINT16_MAX, "amqp_basic_qos prefetch_count is uint16_t");

// aggregate
inline constexpr uint64_t kMsinDivisor = 10'000'000'000ULL; // strips MSIN, leaves MCCMNC

