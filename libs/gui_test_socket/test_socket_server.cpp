/*
 * SPDX-License-Identifier: MIT
 */

#include "test_socket_server.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <utility>

#include <line_buffer.h>
#include <reply_format.h>

#include "remove_on_signal.h"
#include <test_command.h>

namespace bitoscope::gui_test_socket {

namespace {

using bitoscope::gui_test_input::errorReply;
using bitoscope::gui_test_input::LineBuffer;
using bitoscope::gui_test_input::parseCommand;

constexpr int kSendTimeoutSeconds = 5;

std::string systemError(const char* call, int error) { return std::format("{} failed: {}", call, std::strerror(error)); }

bool fillAddress(const std::string& path, sockaddr_un& address) {
  address = sockaddr_un{};
  address.sun_family = AF_UNIX;
  if (path.empty() || path.size() >= sizeof(address.sun_path)) {
    return false;
  }
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  return true;
}

template <typename Call>
auto retrying(Call call) {
  for (;;) {
    const auto result = call();
    if (result >= 0 || errno != EINTR) {
      return result;
    }
  }
}

bool writeLine(int fd, const std::string& line) {
  const std::string bytes = line + "\n";
  std::size_t sent = 0;
  while (sent < bytes.size()) {
    // WHY: MSG_NOSIGNAL: a vanished client must be EPIPE here, not a SIGPIPE that ends the program.
    const ssize_t n = retrying([&] { return send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL); });
    if (n <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(n);
  }
  return true;
}

bool someoneListens(const sockaddr_un& address) {
  const int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (probe < 0) {
    return false;
  }
  const bool listening =
    retrying([&] { return connect(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)); }) == 0;
  close(probe);
  return listening;
}

}

std::expected<void, std::string> ensurePrivateDirectory(const std::string& directory, uid_t uid) {
  if (mkdir(directory.c_str(), 0700) == 0) {
    // WHY: mkdir applies the umask, which can only remove bits from 0700; chmod sets it exactly.
    if (chmod(directory.c_str(), 0700) != 0) {
      return std::unexpected(systemError("chmod", errno));
    }
    return {};
  }
  if (errno != EEXIST) {
    return std::unexpected(std::format("cannot create {}: {}", directory, std::strerror(errno)));
  }
  struct stat info {};
  if (lstat(directory.c_str(), &info) != 0) {
    return std::unexpected(std::format("cannot read {}: {}", directory, std::strerror(errno)));
  }
  if (!S_ISDIR(info.st_mode)) {
    return std::unexpected(std::format("{} exists and is not a directory (a symbolic link is not accepted)", directory));
  }
  if (info.st_uid != uid) {
    return std::unexpected(std::format("{} is owned by user {}, not by user {}", directory, info.st_uid, uid));
  }
  if ((info.st_mode & 077) != 0) {
    return std::unexpected(std::format("{} has mode {:o}; it must give group and others no access (700)", directory,
                                       info.st_mode & 0777));
  }
  return {};
}

TestSocketServer::TestSocketServer(std::string path, bitoscope::gui_test_input::CommandChannel& channel,
                                   uid_t allowedUid)
    : path_(std::move(path)), channel_(channel), allowedUid_(allowedUid) {}

TestSocketServer::~TestSocketServer() { stop(); }

std::expected<void, std::string> TestSocketServer::start() {
  sockaddr_un address{};
  if (!fillAddress(path_, address)) {
    return std::unexpected(std::format("the socket path {} is empty or longer than {} bytes", path_,
                                       sizeof(address.sun_path) - 1));
  }

  struct stat existing {};
  if (lstat(path_.c_str(), &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode)) {
      return std::unexpected(std::format("{} exists and is not a socket; it is left alone", path_));
    }
    if (someoneListens(address)) {
      return std::unexpected(std::format("{} is in use: another process listens on it", path_));
    }
    if (unlink(path_.c_str()) != 0 && errno != ENOENT) {
      return std::unexpected(std::format("the stale socket {} cannot be removed: {}", path_, std::strerror(errno)));
    }
    replacedStale_ = true;
  } else if (errno != ENOENT) {
    return std::unexpected(std::format("cannot read {}: {}", path_, std::strerror(errno)));
  }

  listenFd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listenFd_ < 0) {
    return std::unexpected(systemError("socket", errno));
  }
  const auto fail = [this](std::string message) {
    close(listenFd_);
    listenFd_ = -1;
    if (bound_) {
      unlink(path_.c_str());
      bound_ = false;
    }
    return std::unexpected(std::move(message));
  };
  if (bind(listenFd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    return fail(systemError("bind", errno));
  }
  bound_ = true;
  if (chmod(path_.c_str(), 0600) != 0) {
    return fail(systemError("chmod", errno));
  }
  struct stat bound {};
  if (lstat(path_.c_str(), &bound) != 0) {
    return fail(systemError("lstat", errno));
  }
  boundDevice_ = bound.st_dev;
  boundInode_ = bound.st_ino;
  if (listen(listenFd_, 4) != 0) {
    return fail(systemError("listen", errno));
  }
  int pipeFds[2] = {-1, -1};
  if (pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK) != 0) {
    return fail(systemError("pipe2", errno));
  }
  stopRead_ = pipeFds[0];
  stopWrite_ = pipeFds[1];
  thread_ = std::thread([this] { run(); });
  return {};
}

std::expected<std::vector<int>, std::string> TestSocketServer::removeFileOnTerminationSignal() {
  if (!bound_) {
    return std::unexpected(std::string("socket is not bound: call start() first"));
  }
  auto armed = armFileRemovalOnSignal(path_);
  if (armed) {
    removalArmed_ = true;
  }
  return armed;
}

void TestSocketServer::stop() {
  if (removalArmed_) {
    // WHY: Disarm BEFORE the file is removed: from here a signal ends the program the default way.
    disarmFileRemovalOnSignal();
    removalArmed_ = false;
  }
  if (thread_.joinable()) {
    const char byte = 'x';
    (void)!write(stopWrite_, &byte, 1);
    thread_.join();
  }
  for (int* fd : {&stopRead_, &stopWrite_, &listenFd_}) {
    if (*fd >= 0) {
      close(*fd);
      *fd = -1;
    }
  }
  if (bound_) {
    // WHY: Remove only the file this server bound: a successor may have bound the name since.
    struct stat now {};
    if (lstat(path_.c_str(), &now) == 0 && now.st_dev == boundDevice_ && now.st_ino == boundInode_) {
      unlink(path_.c_str());
    }
    bound_ = false;
  }
}

bool TestSocketServer::peerAllowed(int client) const {
  ucred credentials{};
  socklen_t size = sizeof(credentials);
  if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0 || size != sizeof(credentials)) {
    return false;
  }
  return credentials.uid == allowedUid_;
}

void TestSocketServer::run() {
  for (;;) {
    pollfd fds[2] = {{listenFd_, POLLIN, 0}, {stopRead_, POLLIN, 0}};
    if (retrying([&] { return poll(fds, 2, -1); }) < 0) {
      return;
    }
    if (fds[1].revents != 0) {
      return;
    }
    if ((fds[0].revents & POLLIN) == 0) {
      continue;
    }
    const int client = retrying([&] { return accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC); });
    if (client < 0) {
      continue;
    }
    if (peerAllowed(client)) {
      serve(client);
    }
    close(client);
  }
}

void TestSocketServer::turnAway(int client) {
  if (peerAllowed(client)) {
    writeLine(client, errorReply("another client is connected; the hook serves one client at a time"));
  }
  close(client);
}

void TestSocketServer::serve(int client) {
  const timeval sendTimeout{kSendTimeoutSeconds, 0};
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
  LineBuffer lines;
  char chunk[4096];
  for (;;) {
    pollfd fds[3] = {{client, POLLIN, 0}, {listenFd_, POLLIN, 0}, {stopRead_, POLLIN, 0}};
    if (retrying([&] { return poll(fds, 3, -1); }) < 0) {
      return;
    }
    if (fds[2].revents != 0) {
      return;
    }
    // WHY: Read the client first: one that has left frees the hook for whoever connected next, who must be served.
    ssize_t read = 0;
    if (fds[0].revents != 0) {
      read = retrying([&] { return recv(client, chunk, sizeof(chunk), 0); });
      if (read <= 0) {
        return;
      }
    }
    if ((fds[1].revents & POLLIN) != 0) {
      if (const int other = retrying([&] { return accept4(listenFd_, nullptr, nullptr, SOCK_CLOEXEC); });
          other >= 0) {
        turnAway(other);
      }
    }
    if (read == 0) {
      continue;
    }
    for (const std::string& line : lines.push(std::string_view(chunk, static_cast<std::size_t>(read)))) {
      const auto command = parseCommand(line);
      const std::string reply = command ? channel_.submitAndWait(*command) : errorReply(command.error());
      if (!writeLine(client, reply)) {
        return;
      }
    }
    if (lines.overflowed()) {
      writeLine(client, errorReply("a command line is longer than 8192 bytes; closing"));
      return;
    }
  }
}

}
