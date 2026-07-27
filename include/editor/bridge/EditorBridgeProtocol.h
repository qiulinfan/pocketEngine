#ifndef EDITOR_BRIDGE_PROTOCOL_H
#define EDITOR_BRIDGE_PROTOCOL_H

#include <string>

namespace EditorBridge {

inline constexpr int kProtocolVersion = 1;

enum class MessageType {
    Request,
    Response,
    Event
};

struct Message {
    int protocol_version = 0;
    std::string id;
    MessageType type = MessageType::Event;
    std::string method;
    std::string payload_json = "{}";
};

bool ParseMessage(const std::string &line, Message &out_message,
                  std::string &out_error);

bool BuildRequest(const std::string &id, const std::string &method,
                  const std::string &payload_json, std::string &out_line,
                  std::string &out_error);

const char *MessageTypeName(MessageType type);

} // namespace EditorBridge

#endif
