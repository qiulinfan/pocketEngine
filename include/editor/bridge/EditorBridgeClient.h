#ifndef EDITOR_BRIDGE_CLIENT_H
#define EDITOR_BRIDGE_CLIENT_H

#include "editor/bridge/EditorBridgeProtocol.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class EditorBridgeClient {
public:
    EditorBridgeClient();
    ~EditorBridgeClient();

    EditorBridgeClient(const EditorBridgeClient &) = delete;
    EditorBridgeClient &operator=(const EditorBridgeClient &) = delete;

    bool Start(const std::filesystem::path &node_executable,
               const std::filesystem::path &host_entry,
               std::string &out_error);
    void Stop();
    bool IsRunning() const;

    bool SendRequest(const std::string &method,
                     const std::string &payload_json,
                     std::string *out_request_id = nullptr);
    std::vector<EditorBridge::Message> DrainMessages();
    std::string GetLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
