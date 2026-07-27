#include "editor/ai/AIEditorService.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#ifndef POCKET_ENGINE_TEST_HOST_ENTRY
#define POCKET_ENGINE_TEST_HOST_ENTRY ""
#endif

namespace {

template <typename Predicate>
bool PumpUntil(AIEditorService &service, Predicate predicate,
               int timeout_milliseconds = 3000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        service.Update();
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    service.Update();
    return predicate();
}

bool Expect(bool condition, const std::string &message,
            const AIEditorService &service) {
    if (condition) return true;
    std::cerr << "FAILED: " << message;
    if (!service.GetLastError().empty()) {
        std::cerr << " (" << service.GetLastError() << ")";
    }
    std::cerr << std::endl;
    return false;
}

} // namespace

int main() {
    const std::filesystem::path host_entry = POCKET_ENGINE_TEST_HOST_ENTRY;
    if (host_entry.empty() || !std::filesystem::exists(host_entry)) {
        std::cout << "SKIPPED: build pocket-agent-host before smoke test."
                  << std::endl;
        return 77;
    }

#if defined(_WIN32)
    _putenv_s("POCKET_AGENT_HOST_ENTRY", host_entry.string().c_str());
#else
    setenv("POCKET_AGENT_HOST_ENTRY", host_entry.string().c_str(), 1);
#endif

    AIEditorService service;
    if (!Expect(service.Start("Projects/Default"),
                "service should start the sidecar", service)) {
        return 1;
    }
    if (!Expect(PumpUntil(service, [&]() {
                    return service.GetState() == AIEditorServiceState::Ready;
                }),
                "sidecar should become ready", service)) {
        return 1;
    }
    if (!Expect(service.StartFakeSession(),
                "Fake Agent session should start", service) ||
        !Expect(PumpUntil(service, [&]() {
                    return service.GetState() == AIEditorServiceState::Idle &&
                           service.HasActiveSession();
                }),
                "Fake Agent session should become idle", service)) {
        return 1;
    }
    if (!Expect(service.SendMessage("phase zero smoke test"),
                "message should be accepted", service) ||
        !Expect(PumpUntil(service, [&]() {
                    return service.GetState() == AIEditorServiceState::Idle &&
                           service.GetConversation().size() == 2;
                }),
                "Fake Agent turn should stream and complete", service)) {
        return 1;
    }

    const std::vector<AIConversationMessage> &conversation =
        service.GetConversation();
    if (!Expect(conversation[1].text.find("phase zero smoke test") !=
                    std::string::npos,
                "assistant response should contain the prompt", service)) {
        return 1;
    }

    service.CloseSession();
    Expect(PumpUntil(service, [&]() { return !service.HasActiveSession(); }),
           "Fake Agent session should close", service);
    service.Stop();
    std::cout << "AI editor service smoke test passed." << std::endl;
    return 0;
}
