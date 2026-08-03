#pragma once

#include <cstddef>
#include <string_view>

inline constexpr std::string_view kConfigPath = "config.toml";

// CDR record
inline constexpr std::size_t kFieldCount = 12;      // number of fields in a CDR record
inline constexpr std::size_t kMaxImsiDigits = 15;   // maximum digits in IMSI
inline constexpr std::size_t kMaxMsisdnDigits = 15; // maximum digits in MSISDN

// file source
inline constexpr std::size_t kBatchSize = 4096;
