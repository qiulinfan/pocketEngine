#include "editor/bridge/EditorBridgeClient.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

std::string BuildSyntheticErrorPayload(const std::string &code,
                                       const std::string &message) {
    std::string escaped_message;
    escaped_message.reserve(message.size());
    for (const char ch : message) {
        switch (ch) {
        case '\\': escaped_message += "\\\\"; break;
        case '"': escaped_message += "\\\""; break;
        case '\n': escaped_message += "\\n"; break;
        case '\r': escaped_message += "\\r"; break;
        case '\t': escaped_message += "\\t"; break;
        default: escaped_message += ch; break;
        }
    }
    return "{\"code\":\"" + code + "\",\"message\":\"" +
           escaped_message + "\"}";
}

#if defined(_WIN32)
std::wstring QuoteWindowsArgument(const std::wstring &argument) {
    std::wstring quoted = L"\"";
    std::size_t backslash_count = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') {
            ++backslash_count;
            continue;
        }
        if (ch == L'"') {
            quoted.append(backslash_count * 2 + 1, L'\\');
            quoted.push_back(L'"');
            backslash_count = 0;
            continue;
        }
        quoted.append(backslash_count, L'\\');
        backslash_count = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslash_count * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}
#endif

} // namespace

struct EditorBridgeClient::Impl {
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> next_request_id{1};
    std::thread reader_thread;
    mutable std::mutex write_mutex;
    mutable std::mutex message_mutex;
    mutable std::mutex error_mutex;
    std::vector<EditorBridge::Message> messages;
    std::string last_error;

#if defined(_WIN32)
    HANDLE child_process = nullptr;
    HANDLE child_thread = nullptr;
    HANDLE child_stdin_write = nullptr;
    HANDLE child_stdout_read = nullptr;
#else
    pid_t child_pid = -1;
    int child_stdin_fd = -1;
    int child_stdout_fd = -1;
#endif

    void SetError(const std::string &error) {
        std::lock_guard<std::mutex> lock(error_mutex);
        last_error = error;
    }

    void PushMessage(EditorBridge::Message message) {
        std::lock_guard<std::mutex> lock(message_mutex);
        messages.emplace_back(std::move(message));
    }

    void PushProtocolError(const std::string &error) {
        EditorBridge::Message message;
        message.protocol_version = EditorBridge::kProtocolVersion;
        message.id = "bridge_error";
        message.type = EditorBridge::MessageType::Event;
        message.method = "host.error";
        message.payload_json =
            BuildSyntheticErrorPayload("HOST_PROTOCOL_INVALID_MESSAGE", error);
        PushMessage(std::move(message));
    }

    void ProcessReadChunk(const char *data, std::size_t size,
                          std::string &buffer) {
        buffer.append(data, size);
        std::size_t newline_position = std::string::npos;
        while ((newline_position = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline_position);
            buffer.erase(0, newline_position + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            EditorBridge::Message message;
            std::string parse_error;
            if (!EditorBridge::ParseMessage(line, message, parse_error)) {
                PushProtocolError(parse_error);
                continue;
            }
            PushMessage(std::move(message));
        }
    }

    void ReaderLoop() {
        std::string buffer;
        char chunk[4096];
#if defined(_WIN32)
        while (true) {
            DWORD bytes_read = 0;
            const BOOL read_ok = ReadFile(child_stdout_read, chunk,
                                          static_cast<DWORD>(sizeof(chunk)),
                                          &bytes_read, nullptr);
            if (!read_ok || bytes_read == 0) break;
            ProcessReadChunk(chunk, static_cast<std::size_t>(bytes_read), buffer);
        }
#else
        while (true) {
            const ssize_t bytes_read =
                read(child_stdout_fd, chunk, sizeof(chunk));
            if (bytes_read > 0) {
                ProcessReadChunk(chunk, static_cast<std::size_t>(bytes_read),
                                 buffer);
                continue;
            }
            if (bytes_read < 0 && errno == EINTR) continue;
            break;
        }
#endif
        if (!buffer.empty()) PushProtocolError("Sidecar closed with a partial JSONL message.");
        running.store(false);
    }

    bool WriteLine(const std::string &line) {
        std::lock_guard<std::mutex> lock(write_mutex);
        if (!running.load()) return false;
        const std::string framed_line = line + "\n";
        std::size_t offset = 0;
        while (offset < framed_line.size()) {
#if defined(_WIN32)
            DWORD bytes_written = 0;
            const DWORD remaining = static_cast<DWORD>(framed_line.size() - offset);
            const BOOL write_ok =
                WriteFile(child_stdin_write, framed_line.data() + offset,
                          remaining, &bytes_written, nullptr);
            if (!write_ok || bytes_written == 0) {
                SetError("Failed to write to pocket-agent-host.");
                return false;
            }
            offset += static_cast<std::size_t>(bytes_written);
#else
            const ssize_t bytes_written =
                write(child_stdin_fd, framed_line.data() + offset,
                      framed_line.size() - offset);
            if (bytes_written > 0) {
                offset += static_cast<std::size_t>(bytes_written);
                continue;
            }
            if (bytes_written < 0 && errno == EINTR) continue;
            SetError(std::string("Failed to write to pocket-agent-host: ") +
                     std::strerror(errno));
            return false;
#endif
        }
        return true;
    }
};

EditorBridgeClient::EditorBridgeClient() : impl_(std::make_unique<Impl>()) {}

EditorBridgeClient::~EditorBridgeClient() {
    Stop();
}

bool EditorBridgeClient::Start(const std::filesystem::path &node_executable,
                               const std::filesystem::path &host_entry,
                               std::string &out_error) {
    Stop();
    if (node_executable.empty()) {
        out_error = "Node executable path is empty.";
        return false;
    }
    if (host_entry.empty() || !std::filesystem::exists(host_entry)) {
        out_error = "pocket-agent-host entry was not found: " +
                    host_entry.string();
        return false;
    }

#if defined(_WIN32)
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attributes.bInheritHandle = TRUE;

    HANDLE child_stdin_read = nullptr;
    HANDLE child_stdout_write = nullptr;
    if (!CreatePipe(&child_stdin_read, &impl_->child_stdin_write,
                    &security_attributes, 0) ||
        !CreatePipe(&impl_->child_stdout_read, &child_stdout_write,
                    &security_attributes, 0)) {
        out_error = "Could not create sidecar pipes.";
        return false;
    }
    SetHandleInformation(impl_->child_stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl_->child_stdout_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(STARTUPINFOW);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = child_stdin_read;
    startup_info.hStdOutput = child_stdout_write;
    startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION process_info{};
    std::wstring command_line =
        QuoteWindowsArgument(node_executable.wstring()) + L" " +
        QuoteWindowsArgument(host_entry.wstring());
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');
    const BOOL created = CreateProcessW(
        nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup_info, &process_info);
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);
    if (!created) {
        CloseHandle(impl_->child_stdin_write);
        CloseHandle(impl_->child_stdout_read);
        impl_->child_stdin_write = nullptr;
        impl_->child_stdout_read = nullptr;
        out_error = "Could not start pocket-agent-host.";
        return false;
    }
    impl_->child_process = process_info.hProcess;
    impl_->child_thread = process_info.hThread;
#else
    static std::once_flag ignore_sigpipe_once;
    std::call_once(ignore_sigpipe_once,
                   []() { std::signal(SIGPIPE, SIG_IGN); });

    int parent_to_child[2] = {-1, -1};
    int child_to_parent[2] = {-1, -1};
    if (pipe(parent_to_child) != 0 || pipe(child_to_parent) != 0) {
        if (parent_to_child[0] >= 0) close(parent_to_child[0]);
        if (parent_to_child[1] >= 0) close(parent_to_child[1]);
        if (child_to_parent[0] >= 0) close(child_to_parent[0]);
        if (child_to_parent[1] >= 0) close(child_to_parent[1]);
        out_error = "Could not create sidecar pipes.";
        return false;
    }

    const pid_t child_pid = fork();
    if (child_pid < 0) {
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        out_error = "Could not fork pocket-agent-host.";
        return false;
    }
    if (child_pid == 0) {
        dup2(parent_to_child[0], STDIN_FILENO);
        dup2(child_to_parent[1], STDOUT_FILENO);
        close(parent_to_child[0]);
        close(parent_to_child[1]);
        close(child_to_parent[0]);
        close(child_to_parent[1]);
        execlp(node_executable.string().c_str(),
               node_executable.string().c_str(), host_entry.string().c_str(),
               static_cast<char *>(nullptr));
        _exit(127);
    }

    close(parent_to_child[0]);
    close(child_to_parent[1]);
    impl_->child_pid = child_pid;
    impl_->child_stdin_fd = parent_to_child[1];
    impl_->child_stdout_fd = child_to_parent[0];
#endif

    impl_->running.store(true);
    impl_->SetError("");
    impl_->reader_thread = std::thread([this]() { impl_->ReaderLoop(); });
    out_error.clear();
    return true;
}

void EditorBridgeClient::Stop() {
    if (!impl_) return;
    if (impl_->running.load()) {
        std::string shutdown_line;
        std::string build_error;
        if (EditorBridge::BuildRequest("editor_shutdown", "host.shutdown", "{}",
                                       shutdown_line, build_error)) {
            impl_->WriteLine(shutdown_line);
        }
    }

#if defined(_WIN32)
    if (impl_->child_stdin_write != nullptr) {
        CloseHandle(impl_->child_stdin_write);
        impl_->child_stdin_write = nullptr;
    }
    if (impl_->child_process != nullptr) {
        const DWORD wait_result =
            WaitForSingleObject(impl_->child_process, 1000);
        if (wait_result == WAIT_TIMEOUT) {
            TerminateProcess(impl_->child_process, 1);
            WaitForSingleObject(impl_->child_process, 1000);
        }
    }
#else
    if (impl_->child_stdin_fd >= 0) {
        close(impl_->child_stdin_fd);
        impl_->child_stdin_fd = -1;
    }
    if (impl_->child_pid > 0) {
        int status = 0;
        bool child_exited = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            const pid_t wait_result = waitpid(impl_->child_pid, &status, WNOHANG);
            if (wait_result == impl_->child_pid) {
                child_exited = true;
                break;
            }
            if (wait_result < 0 && errno != EINTR) {
                child_exited = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (!child_exited) {
            kill(impl_->child_pid, SIGTERM);
            while (waitpid(impl_->child_pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }
#endif

    impl_->running.store(false);
    if (impl_->reader_thread.joinable()) impl_->reader_thread.join();

#if defined(_WIN32)
    if (impl_->child_stdout_read != nullptr) {
        CloseHandle(impl_->child_stdout_read);
        impl_->child_stdout_read = nullptr;
    }
    if (impl_->child_thread != nullptr) {
        CloseHandle(impl_->child_thread);
        impl_->child_thread = nullptr;
    }
    if (impl_->child_process != nullptr) {
        CloseHandle(impl_->child_process);
        impl_->child_process = nullptr;
    }
#else
    if (impl_->child_stdout_fd >= 0) {
        close(impl_->child_stdout_fd);
        impl_->child_stdout_fd = -1;
    }
    impl_->child_pid = -1;
#endif
}

bool EditorBridgeClient::IsRunning() const {
    return impl_ && impl_->running.load();
}

bool EditorBridgeClient::SendRequest(const std::string &method,
                                     const std::string &payload_json,
                                     std::string *out_request_id) {
    if (!impl_ || !impl_->running.load()) return false;
    const std::string request_id =
        "editor_" + std::to_string(impl_->next_request_id.fetch_add(1));
    std::string line;
    std::string build_error;
    if (!EditorBridge::BuildRequest(request_id, method, payload_json, line,
                                    build_error)) {
        impl_->SetError(build_error);
        return false;
    }
    if (!impl_->WriteLine(line)) return false;
    if (out_request_id != nullptr) *out_request_id = request_id;
    return true;
}

std::vector<EditorBridge::Message> EditorBridgeClient::DrainMessages() {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->message_mutex);
    std::vector<EditorBridge::Message> drained;
    drained.swap(impl_->messages);
    return drained;
}

std::string EditorBridgeClient::GetLastError() const {
    if (!impl_) return "Editor bridge is unavailable.";
    std::lock_guard<std::mutex> lock(impl_->error_mutex);
    return impl_->last_error;
}
