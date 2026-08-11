#include "aggregate/aggregator.hpp"

#include "aggregate/delta.hpp"
#include "cdr_record.hpp"
#include "constants.hpp"

namespace cdrp {

void Aggregator::fold(const std::vector<CdrRecord>& batch, Delta& out) const
{
    out.subs.clear();
    out.ops.clear();
    out.links.clear();

    for (const CdrRecord& r : batch) {
        if (r.subscriberMSISDN == 0) {
            continue;
        }

        SubDelta& sub = out.subs[r.subscriberMSISDN];

        const OpKey_t mccmnc = static_cast<OpKey_t>(r.subscriberImsi / kMsinDivisor);
        OpDelta* op = mccmnc ? &out.ops[mccmnc] : nullptr;

        switch (r.usageType) {
            case UsageType::MOC:
                sub.voice_out += r.duration;
                if (op) {
                    op->voice_out += r.duration;
                }
                addLink(out.links, r.subscriberMSISDN, r.secondPartyMSISDN, r.duration, 0, 1);
                break;

            case UsageType::MTC:
                sub.voice_in += r.duration;
                if (op) {
                    op->voice_in += r.duration;
                }
                addLink(out.links, r.subscriberMSISDN, r.secondPartyMSISDN, r.duration, 0, 1);
                break;

            case UsageType::SMS_MO:
                ++sub.sms_out;
                if (op) {
                    ++op->sms_out;
                }
                addLink(out.links, r.subscriberMSISDN, r.secondPartyMSISDN, 0, 1, 0);
                break;

            case UsageType::SMS_MT:
                ++sub.sms_in;
                if (op) {
                    ++op->sms_in;
                }
                addLink(out.links, r.subscriberMSISDN, r.secondPartyMSISDN, 0, 1, 0);
                break;

            case UsageType::D:
                sub.data_rx += r.bytesReceived;
                sub.data_tx += r.bytesTransmitted;
                break;

            case UsageType::U:
                ++sub.noans;
                break;

            case UsageType::B:
                ++sub.busy;
                break;

            case UsageType::X:
                ++sub.failed;
                break;
        }
    }
}

void Aggregator::addLink(LinkMap& links, uint64_t a, uint64_t b, uint64_t dur, uint64_t sms,
                         uint64_t cnt)
{
    if (b == 0) {
        return;
    }

    LinkDelta& fwd = links[LinkKey{a, b}];
    fwd.dur += dur;
    fwd.sms += sms;
    fwd.cnt += cnt;

    LinkDelta& rev = links[LinkKey{b, a}];
    rev.dur += dur;
    rev.sms += sms;
    rev.cnt += cnt;
}

} // namespace cdrp

