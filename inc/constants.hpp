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

// redis store
inline constexpr std::size_t kRedisPipelineDepth = 1024; // commands queued before a drain

// aggregate keys
inline constexpr std::string_view kSubPrefix = "sub:";
inline constexpr std::string_view kOpPrefix = "op:";
inline constexpr std::string_view kLinkPrefix = "link:";

// aggregate fields
inline constexpr std::string_view kFieldVoiceOut = "voice_out";
inline constexpr std::string_view kFieldVoiceIn = "voice_in";
inline constexpr std::string_view kFieldDataRx = "data_rx";
inline constexpr std::string_view kFieldDataTx = "data_tx";
inline constexpr std::string_view kFieldSmsOut = "sms_out";
inline constexpr std::string_view kFieldSmsIn = "sms_in";
inline constexpr std::string_view kFieldNoans = "noans";
inline constexpr std::string_view kFieldBusy = "busy";
inline constexpr std::string_view kFieldFailed = "failed";
inline constexpr std::string_view kFieldDurSuffix = ":dur";
inline constexpr std::string_view kFieldSmsSuffix = ":sms";

// totals key
inline constexpr std::string_view kTotalKey = "total:proc";

// totals fields, the usage types rather than the aggregate names
inline constexpr std::string_view kFieldRecords = "records";
inline constexpr std::string_view kFieldMocCnt = "moc_cnt";
inline constexpr std::string_view kFieldMtcCnt = "mtc_cnt";
inline constexpr std::string_view kFieldSmsMoCnt = "sms_mo_cnt";
inline constexpr std::string_view kFieldSmsMtCnt = "sms_mt_cnt";
inline constexpr std::string_view kFieldDataCnt = "data_cnt";
inline constexpr std::string_view kFieldNoansCnt = "noans_cnt";
inline constexpr std::string_view kFieldBusyCnt = "busy_cnt";
inline constexpr std::string_view kFieldFailedCnt = "failed_cnt";
inline constexpr std::string_view kFieldMocDur = "moc_dur";
inline constexpr std::string_view kFieldMtcDur = "mtc_dur";
inline constexpr std::string_view kFieldDataDur = "data_dur";
inline constexpr std::size_t kTotalsNameWidth = 12; // values line up past the longest name

// json query
inline constexpr uint64_t kBytesPerKb = 1024;
inline constexpr std::size_t kMaxHops = 6;
inline constexpr std::size_t kMaxVisited = 10'000;
inline constexpr std::string_view kJsonVoiceOut = "voice-out";
inline constexpr std::string_view kJsonVoiceIn = "voice-in";
inline constexpr std::string_view kJsonDataOut = "data-out";
inline constexpr std::string_view kJsonDataIn = "data-in";
inline constexpr std::string_view kJsonSmsOut = "sms-out";
inline constexpr std::string_view kJsonSmsIn = "sms-in";
inline constexpr std::string_view kJsonNoans = "no-answer";
inline constexpr std::string_view kJsonBusy = "busy";
inline constexpr std::string_view kJsonFailed = "failed";
inline constexpr std::string_view kJsonDuration = "duration";
inline constexpr std::string_view kJsonSms = "sms";
