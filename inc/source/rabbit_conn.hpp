#pragma once
#include <cstdint>
#include <string>

#include "rabbitmq-c/amqp.h"

namespace cdrp {

/**
 * Holds one AMQP connection and the channel it consumes on.
 * Every call blocks on the socket, so a connection belongs to one thread.
 * A connection that was never opened, or failed to open, fails every call.
 */
class RabbitConn {
public:
    /* What a call got back: a message, nothing within the timeout, or a broken connection */
    enum class Status {
        OK,
        TIMEOUT,
        FAIL
    };

    /* One delivery: its body, the type the publisher set, and the tag ack() takes */
    struct Message {
        std::string body;
        std::string type;
        uint64_t tag = 0;
    };

    RabbitConn() = default;
    ~RabbitConn();

    RabbitConn(const RabbitConn&) = delete;
    RabbitConn& operator=(const RabbitConn&) = delete;

    /**
     * Connects, logs in, opens a channel and starts consuming the queue.
     * Closes whatever was open before, and leaves nothing open when a step fails.
     *
     * @param url: the amqp url of the broker, with user, password and vhost
     * @param queue: the queue to consume
     * @param prefetch: how many unacked messages the broker may send ahead
     * @return true once the queue is being consumed
     */
    bool open(const std::string& url, const std::string& queue, uint16_t prefetch);

    /**
     * Waits for the next message on the channel.
     *
     * @param out: the message, filled only when the return is OK
     * @param timeout_ms: how long to wait before giving up on this call
     * @return OK with a message, TIMEOUT when none arrived, FAIL when the connection is gone
     */
    Status consume(Message& out, int timeout_ms);

    /**
     * Tells the broker the message is handled and can be dropped.
     *
     * @param tag: the delivery tag of the message
     * @param multiple: ack every message up to and including this tag
     * @return true when the ack reached the socket
     */
    bool ack(uint64_t tag, bool multiple);

private:
    /* Close the channel and the connection, safe when nothing is open */
    void close();

private:
    amqp_connection_state_t m_conn = nullptr;
    bool m_channel_open = false;
    static constexpr amqp_channel_t kChannel = 1;
};

} // namespace cdrp

