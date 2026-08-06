#pragma once

#include <cstdint>
#include <string>
#include <ctime>

namespace cdrp {

enum class UsageType {
    MOC,    // outgoing voice call
    MTC,    // incoming voice call
    SMS_MO, // outgoing message
    SMS_MT, // incoming message
    D,      // Data
    U,      // call not answered
    B,      // busy call
    X,      // failed call
};

/* cdr record structure */
struct CdrRecord {
    uint64_t sequence;
    uint64_t subscriberImsi;
    std::string subscriberImei;
    UsageType usageType;
    uint64_t subscriberMSISDN;
    std::time_t callTime;       // combined date+time
    uint64_t duration;          // in seconds
    uint64_t bytesReceived;     // if type is Data
    uint64_t bytesTransmitted;  // if type is Data
    uint64_t secondPartyIMSI;   // 0 if empty
    uint64_t secondPartyMSISDN; // 0 if empty
};

} // namespace cdrp

