#include "editor/bridge/EditorBridgeProtocol.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef POCKET_ENGINE_TEST_FIXTURE_DIR
#define POCKET_ENGINE_TEST_FIXTURE_DIR "."
#endif

namespace {

bool Expect(bool condition, const char *message) {
    if (condition) return true;
    std::cerr << "FAILED: " << message << std::endl;
    return false;
}

std::string ReadFixture(const std::string &name) {
    const std::string path =
        std::string(POCKET_ENGINE_TEST_FIXTURE_DIR) + "/" + name;
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

bool TestInitializeFixture() {
    EditorBridge::Message message;
    std::string error;
    const std::string fixture =
        ReadFixture("host-initialize.request.jsonl");
    return Expect(!fixture.empty(), "initialize fixture should exist") &&
           Expect(EditorBridge::ParseMessage(fixture, message, error),
                  "initialize fixture should parse") &&
           Expect(message.protocol_version == EditorBridge::kProtocolVersion,
                  "protocol version should match") &&
           Expect(message.type == EditorBridge::MessageType::Request,
                  "fixture should be a request") &&
           Expect(message.method == "host.initialize",
                  "fixture method should match") &&
           Expect(message.payload_json.find("Projects/Default") !=
                      std::string::npos,
                  "fixture payload should be preserved");
}

bool TestRequestRoundTrip() {
    std::string line;
    std::string error;
    if (!Expect(EditorBridge::BuildRequest(
                    "editor_9", "agent.send_message",
                    "{\"message\":\"hello\"}", line, error),
                "request should serialize")) {
        return false;
    }

    EditorBridge::Message parsed;
    return Expect(EditorBridge::ParseMessage(line, parsed, error),
                  "serialized request should parse") &&
           Expect(parsed.id == "editor_9", "request id should round trip") &&
           Expect(parsed.method == "agent.send_message",
                  "request method should round trip") &&
           Expect(parsed.payload_json.find("hello") != std::string::npos,
                  "request payload should round trip");
}

bool TestRejectsInvalidMessages() {
    EditorBridge::Message message;
    std::string error;
    std::string line;
    return Expect(!EditorBridge::ParseMessage(
                      "{\"protocol_version\":2,\"id\":\"x\","
                      "\"type\":\"event\",\"method\":\"x\","
                      "\"payload\":{}}",
                      message, error),
                  "version mismatch should fail") &&
           Expect(error.find("version") != std::string::npos,
                  "version mismatch should explain failure") &&
           Expect(!EditorBridge::BuildRequest("x", "method", "[]", line,
                                              error),
                  "array payload should fail");
}

} // namespace

int main() {
    bool success = true;
    success &= TestInitializeFixture();
    success &= TestRequestRoundTrip();
    success &= TestRejectsInvalidMessages();
    if (!success) return 1;
    std::cout << "Editor Bridge protocol tests passed." << std::endl;
    return 0;
}
