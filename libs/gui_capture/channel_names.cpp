/*
 * SPDX-License-Identifier: MIT
 */

#include "channel_names.h"

namespace bitoscope::gui_capture {

namespace {

const std::string& nameAt(std::span<const std::string> names, std::uint32_t deviceChannel) {
  static const std::string none;
  return deviceChannel < names.size() ? names[deviceChannel] : none;
}

bool isUtf8Continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u; }

}

std::string shownChannelName(std::span<const std::string> userNames, std::span<const std::string> ownNames,
                             std::uint32_t deviceChannel) {
  const std::string& user = nameAt(userNames, deviceChannel);
  if (!user.empty()) return user;
  return nameAt(ownNames, deviceChannel);
}

std::string cleanedChannelName(std::string_view typed) {
  std::string out;
  out.reserve(typed.size());
  for (const char c : typed) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 0x20u && byte != 0x7Fu) out.push_back(c);
  }
  out.erase(0, out.find_first_not_of(' '));
  if (out.size() > kChannelNameMaxBytes) {
    std::size_t cut = kChannelNameMaxBytes;
    while (cut > 0 && isUtf8Continuation(out[cut])) --cut;
    out.resize(cut);
  }
  out.erase(out.find_last_not_of(' ') + 1);
  return out;
}

bool renameChannel(std::vector<std::string>& userNames, std::span<const std::string> ownNames,
                   std::uint32_t deviceChannel, std::string_view typed) {
  const std::string& own = nameAt(ownNames, deviceChannel);
  std::string name = cleanedChannelName(typed);
  if (name.empty()) name = own;
  if (name == shownChannelName(userNames, ownNames, deviceChannel)) return false;
  if (name == own) name.clear();
  if (deviceChannel >= userNames.size()) userNames.resize(static_cast<std::size_t>(deviceChannel) + 1);
  userNames[deviceChannel] = std::move(name);
  return true;
}

}
