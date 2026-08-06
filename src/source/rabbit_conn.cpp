#include "source/rabbit_conn.hpp"

#include "constants.hpp"
#include "logger.hpp"

#include <string>
#include <vector>
#include "rabbitmq-c/tcp_socket.h"

namespace {

std::string to_string(amqp_bytes_t b)
{
    return b.bytes ? std::string(static_cast<char*>(b.bytes), b.len) : "";
}

/* What the broker or the client library said went wrong */
std::string reason_of(amqp_rpc_reply_t reply)
{
    switch (reply.reply_type) {
    case AMQP_RESPONSE_LIBRARY_EXCEPTION:
        return amqp_error_string2(reply.library_error);

    case AMQP_RESPONSE_SERVER_EXCEPTION:
        if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
            const auto* m = static_cast<amqp_connection_close_t*>(reply.reply.decoded);
            return std::to_string(m->reply_code) + " " + to_string(m->reply_text);
        }
        if (reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
            const auto* m = static_cast<amqp_channel_close_t*>(reply.reply.decoded);
            return std::to_string(m->reply_code) + " " + to_string(m->reply_text);
        }
        return "server error " + std::to_string(reply.reply.id);

    case AMQP_RESPONSE_NONE:
        return "no reply";

    default:
        return "unknown reply";
    }
}

bool rpc_ok(amqp_rpc_reply_t reply, const char* what)
{
    if (reply.reply_type == AMQP_RESPONSE_NORMAL) {
        return true;
    }

    cdrp::logError("RabbitConn", std::string(what) + " failed: " + reason_of(reply));
    return false;
}

} // namespace

namespace cdrp {

RabbitConn::~RabbitConn()
{
    close();
}

bool RabbitConn::open(const std::string& url, const std::string& queue)
{
    close();

    std::vector<char> buf(url.begin(), url.end());
    buf.push_back('\0');

    amqp_connection_info info;
    amqp_default_connection_info(&info);
    if (amqp_parse_url(buf.data(), &info) != AMQP_STATUS_OK) {
        logError("RabbitConn", "bad url: " + url);
        return false;
    }

    m_conn = amqp_new_connection();
    if (!m_conn) {
        logError("RabbitConn", "connection failed");
        return false;
    }

    amqp_socket_t* socket = amqp_tcp_socket_new(m_conn);
    if (!socket) {
        logError("RabbitConn", "socket failed");
        close();
        return false;
    }
    const int opened = amqp_socket_open(socket, info.host, info.port);
    if (opened != AMQP_STATUS_OK) {
        logError("RabbitConn", "connect failed " + std::string(info.host) + ":"
            + std::to_string(info.port) + ": " + amqp_error_string2(opened));
        close();
        return false;
    }

    const char* vhost = (info.vhost && *info.vhost) ? info.vhost : "/";

    logDebug("RabbitConn", "login " + std::string(info.user) + "@" + info.host + " vhost " + vhost);

    const auto login = amqp_login(m_conn, vhost, 0, AMQP_DEFAULT_FRAME_SIZE, 0, AMQP_SASL_METHOD_PLAIN, info.user, info.password);
    if (!rpc_ok(login, "login")) {
        close();
        return false;
    }

    amqp_channel_open(m_conn, kChannel);
    if (!rpc_ok(amqp_get_rpc_reply(m_conn), "channel open")) {
        close();
        return false;
    }
    m_channel_open = true;

    amqp_basic_qos(m_conn, kChannel, 0, kRabbitPrefetch, 0);
    if (!rpc_ok(amqp_get_rpc_reply(m_conn), "basic qos")) {
        close();
        return false;
    }

    amqp_basic_consume(m_conn, kChannel, amqp_cstring_bytes(queue.c_str()), amqp_empty_bytes, 0, 0, 0, amqp_empty_table);
    if (!rpc_ok(amqp_get_rpc_reply(m_conn), "basic consume")) {
        close();
        return false;
    }

    logInfo("RabbitConn", "consuming: " + queue + " prefetch " + std::to_string(kRabbitPrefetch));
    return true;
}

RabbitConn::Status RabbitConn::consume(Message& out, int timeout_ms)
{
    if (!m_conn || !m_channel_open) {
        return Status::FAIL;
    }

    amqp_maybe_release_buffers(m_conn);

    timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    amqp_envelope_t env;
    const auto reply = amqp_consume_message(m_conn, &env, &tv, 0);

    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
            reply.library_error == AMQP_STATUS_TIMEOUT) {
            return Status::TIMEOUT;
        }
        logError("RabbitConn", "consume failed: " + reason_of(reply));
        return Status::FAIL;
    }

    out.body = to_string(env.message.body);
    out.type = (env.message.properties._flags & AMQP_BASIC_TYPE_FLAG)
                   ? to_string(env.message.properties.type)
                   : std::string();
    out.tag = env.delivery_tag;

    logDebug("RabbitConn", "message " + std::to_string(out.tag) + ", " + std::to_string(out.body.size()) + " bytes");

    amqp_destroy_envelope(&env);
    return Status::OK;
}

bool RabbitConn::ack(uint64_t tag, bool multiple)
{
    if (!m_conn || !m_channel_open) {
        return false;
    }

    if (amqp_basic_ack(m_conn, kChannel, tag, multiple ? 1 : 0) != AMQP_STATUS_OK) {
        logWarn("RabbitConn", "ack failed for tag " + std::to_string(tag));
        return false;
    }

    return true;
}

void RabbitConn::close()
{
    if (!m_conn) {
        return;
    }
    if (m_channel_open) {
        amqp_channel_close(m_conn, kChannel, AMQP_REPLY_SUCCESS);
        m_channel_open = false;
    }
    amqp_connection_close(m_conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(m_conn);
    m_conn = nullptr;
}

} // namespace cdrp

