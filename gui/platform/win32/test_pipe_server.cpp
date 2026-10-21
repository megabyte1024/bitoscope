/*
 * SPDX-License-Identifier: MIT
 */

#include "test_pipe_server.h"

#ifdef BITOSCOPE_GUI_TEST_HOOK

#include <sddl.h>

#include <chrono>
#include <format>
#include <utility>
#include <vector>

#include <line_buffer.h>
#include <reply_format.h>
#include <test_command.h>

namespace bitoscope::gui::win32 {

namespace {

using bitoscope::gui_test_input::errorReply;
using bitoscope::gui_test_input::LineBuffer;
using bitoscope::gui_test_input::parseCommand;

std::wstring currentUserOnlySddl(std::string& error) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    error = std::format("OpenProcessToken failed ({})", GetLastError());
    return {};
  }
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<unsigned char> buffer(size);
  std::wstring sddl;
  if (size == 0 || !GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
    error = std::format("GetTokenInformation(TokenUser) failed ({})", GetLastError());
  } else {
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sidText)) {
      error = std::format("ConvertSidToStringSid failed ({})", GetLastError());
    } else {
      sddl = std::wstring(L"D:P(A;;GA;;;") + sidText + L")";
      LocalFree(sidText);
    }
  }
  CloseHandle(token);
  return sddl;
}

}

TestPipeServer::TestPipeServer(std::string pipeName, bitoscope::gui_test_input::CommandChannel& channel)
    : pipeName_(std::move(pipeName)), channel_(channel) {}

TestPipeServer::~TestPipeServer() { stop(); }

bool TestPipeServer::start(std::string& error) {
  const std::wstring sddl = currentUserOnlySddl(error);
  if (sddl.empty()) {
    return false;
  }
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    error = std::format("ConvertStringSecurityDescriptorToSecurityDescriptor failed ({})", GetLastError());
    return false;
  }
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.lpSecurityDescriptor = descriptor;
  attributes.bInheritHandle = FALSE;

  const std::wstring path = L"\\\\.\\pipe\\" + std::wstring(pipeName_.begin(), pipeName_.end());
  pipe_ = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                           PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536,
                           65536, 0, &attributes);
  const DWORD createError = GetLastError();
  LocalFree(descriptor);
  if (pipe_ == INVALID_HANDLE_VALUE) {
    error = std::format("CreateNamedPipe failed ({})", createError);
    return false;
  }
  thread_ = std::thread([this] { run(); });
  return true;
}

void TestPipeServer::stop() {
  if (!thread_.joinable()) {
    return;
  }
  for (int waited = 0; replying_ && waited < 200; ++waited) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  stopping_ = true;
  // WHY: Cancel I/O in a loop: the thread may be blocked in ConnectNamedPipe or ReadFile, or about to be.
  while (!finished_) {
    CancelSynchronousIo(thread_.native_handle());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  thread_.join();
  if (pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(pipe_);
    pipe_ = INVALID_HANDLE_VALUE;
  }
}

void TestPipeServer::run() {
  while (!stopping_) {
    const BOOL connected = ConnectNamedPipe(pipe_, nullptr);
    const DWORD error = GetLastError();
    if (stopping_) {
      break;
    }
    if (!connected && error != ERROR_PIPE_CONNECTED) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    serve();
    DisconnectNamedPipe(pipe_);
  }
  finished_ = true;
}

bool TestPipeServer::writeLine(const std::string& line) {
  const std::string bytes = line + "\n";
  DWORD written = 0;
  return WriteFile(pipe_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
         written == bytes.size();
}

void TestPipeServer::serve() {
  LineBuffer lines;
  char chunk[4096];
  while (!stopping_) {
    DWORD read = 0;
    if (!ReadFile(pipe_, chunk, sizeof(chunk), &read, nullptr) || read == 0) {
      return;
    }
    for (const std::string& line : lines.push(std::string_view(chunk, read))) {
      replying_ = true;
      const auto command = parseCommand(line);
      const std::string reply =
        command ? channel_.submitAndWait(*command) : errorReply(command.error());
      const bool written = writeLine(reply);
      replying_ = false;
      if (!written) {
        return;
      }
    }
    if (lines.overflowed()) {
      writeLine(errorReply("a command line is longer than 8192 bytes; closing"));
      return;
    }
  }
}

}

#endif
