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

// rabbit reconnect, doubling from min to max after a lost connection
inline constexpr unsigned kRabbitBackoffMinMs = 250;
inline constexpr unsigned kRabbitBackoffMaxMs = 10000;
inline constexpr unsigned kRabbitBackoffSliceMs = 100; // longest a backoff delays a stop

// aggregate
inline constexpr uint64_t kMsinDivisor = 10'000'000'000ULL; // strips MSIN, leaves MCCMNC

// redis store
inline constexpr std::size_t kRedisPipelineDepth = 1024; // commands queued before a drain
inline constexpr unsigned kRedisBackoffMinMs = 100;      // reconnect wait, doubles to max
inline constexpr unsigned kRedisBackoffMaxMs = 5000;

// aggregate keys
inline constexpr std::string_view kSubPrefix = "sub:";
inline constexpr std::string_view kOpPrefix = "op:";
inline constexpr std::string_view kLinkPrefix = "link:";

// board keys, one sorted set per ranking
inline constexpr std::string_view kVoiceBoard = "top:voice";
inline constexpr std::string_view kSmsBoard = "top:sms";
inline constexpr std::string_view kDataBoard = "top:data";
inline constexpr std::string_view kFailBoard = "top:fail";
inline constexpr std::string_view kOpVoiceBoard = "top:op-voice";
inline constexpr std::string_view kOpSmsBoard = "top:op-sms";

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
inline constexpr std::string_view kFieldCntSuffix = ":cnt";

// totals key
inline constexpr std::string_view kTotalKey = "total:proc";

// progress key, one field per source, the highest sequence applied from it
inline constexpr std::string_view kProgressKey = "prog:file";

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

// query parameters, a default limit and the cap it is clamped to
inline constexpr std::size_t kPeerLimit = 100;
inline constexpr std::size_t kPeerLimitMax = 1000;
inline constexpr std::size_t kTopLimit = 20;
inline constexpr std::size_t kTopLimitMax = 500;
inline constexpr std::string_view kSortDur = "dur";
inline constexpr std::string_view kSortSms = "sms";

// json query
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
inline constexpr std::string_view kJsonCalls = "calls";
inline constexpr std::string_view kJsonMaxHops = "max-hops";
inline constexpr std::string_view kJsonMaxVisited = "max-visited";
