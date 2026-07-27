#include "editor/bridge/EditorBridgeProtocol.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace EditorBridge {
namespace {

bool ParseMessageType(const rapidjson::Value &value, MessageType &out_type) {
    if (!value.IsString()) return false;
    const std::string type_name = value.GetString();
    if (type_name == "request") {
        out_type = MessageType::Request;
        return true;
    }
    if (type_name == "response") {
        out_type = MessageType::Response;
        return true;
    }
    if (type_name == "event") {
        out_type = MessageType::Event;
        return true;
    }
    return false;
}

std::string SerializeValue(const rapidjson::Value &value) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

} // namespace

bool ParseMessage(const std::string &line, Message &out_message,
                  std::string &out_error) {
    rapidjson::Document document;
    document.Parse(line.c_str());
    if (document.HasParseError() || !document.IsObject()) {
        out_error = "Bridge message is not a valid JSON object.";
        return false;
    }
    if (!document.HasMember("protocol_version") ||
        !document["protocol_version"].IsInt()) {
        out_error = "Bridge message is missing protocol_version.";
        return false;
    }
    if (document["protocol_version"].GetInt() != kProtocolVersion) {
        out_error = "Bridge protocol version mismatch.";
        return false;
    }
    if (!document.HasMember("id") || !document["id"].IsString() ||
        document["id"].GetStringLength() == 0) {
        out_error = "Bridge message is missing id.";
        return false;
    }
    if (!document.HasMember("type") ||
        !ParseMessageType(document["type"], out_message.type)) {
        out_error = "Bridge message has an invalid type.";
        return false;
    }
    if (!document.HasMember("method") || !document["method"].IsString() ||
        document["method"].GetStringLength() == 0) {
        out_error = "Bridge message is missing method.";
        return false;
    }
    if (!document.HasMember("payload") || !document["payload"].IsObject()) {
        out_error = "Bridge message payload must be an object.";
        return false;
    }

    out_message.protocol_version = document["protocol_version"].GetInt();
    out_message.id = document["id"].GetString();
    out_message.method = document["method"].GetString();
    out_message.payload_json = SerializeValue(document["payload"]);
    out_error.clear();
    return true;
}

bool BuildRequest(const std::string &id, const std::string &method,
                  const std::string &payload_json, std::string &out_line,
                  std::string &out_error) {
    if (id.empty() || method.empty()) {
        out_error = "Bridge request id and method are required.";
        return false;
    }

    rapidjson::Document payload;
    payload.Parse(payload_json.c_str());
    if (payload.HasParseError() || !payload.IsObject()) {
        out_error = "Bridge request payload must be a JSON object.";
        return false;
    }

    rapidjson::Document request(rapidjson::kObjectType);
    rapidjson::Document::AllocatorType &allocator = request.GetAllocator();
    request.AddMember("protocol_version", kProtocolVersion, allocator);
    request.AddMember("id", rapidjson::Value(id.c_str(), allocator), allocator);
    request.AddMember("type", "request", allocator);
    request.AddMember("method", rapidjson::Value(method.c_str(), allocator),
                      allocator);
    rapidjson::Value payload_copy;
    payload_copy.CopyFrom(payload, allocator);
    request.AddMember("payload", payload_copy, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    request.Accept(writer);
    out_line = buffer.GetString();
    out_error.clear();
    return true;
}

const char *MessageTypeName(MessageType type) {
    switch (type) {
    case MessageType::Request:
        return "request";
    case MessageType::Response:
        return "response";
    case MessageType::Event:
        return "event";
    }
    return "event";
}

} // namespace EditorBridge
