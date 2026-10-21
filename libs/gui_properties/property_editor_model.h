/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ipc_messages.h"
#include "ipc_properties.h"

namespace bitoscope::gui_properties {

namespace props = bitoscope::ipc::props;

using PropertyValue = props::PropertyValue;

enum class PropertyKind { Integer, Decimal, Boolean, Enumeration, Unknown };

struct EnumerationChoice {
  std::string text;
  bool operator==(const EnumerationChoice&) const = default;
};

struct PropertyDisplay {
  std::string id;
  std::string label;
  std::string help;
  PropertyKind kind = PropertyKind::Unknown;
  std::string unitSymbol;
  std::vector<EnumerationChoice> choices;
  std::optional<std::string> minText;
  std::optional<std::string> maxText;
  std::string defaultText;
  bool changeableDuringCapture = false;
  bool operator==(const PropertyDisplay&) const = default;
};

struct PropertyTreeNode;

struct PropertyGroupDisplay {
  std::string name;
  std::vector<PropertyTreeNode> children;
  bool operator==(const PropertyGroupDisplay& other) const;
};

struct PropertyTreeNode {
  std::variant<PropertyGroupDisplay, PropertyDisplay> node;
  bool operator==(const PropertyTreeNode&) const = default;
};

std::vector<PropertyTreeNode> buildPropertyTree(const std::vector<props::PropertyNode>& nodes);

std::vector<const PropertyDisplay*> flattenProperties(const std::vector<PropertyTreeNode>& tree);

class FacetPropertiesModel {
 public:
  explicit FacetPropertiesModel(std::vector<props::PropertyNode> tree);

  const std::vector<PropertyTreeNode>& displayTree() const { return displayTree_; }

  const PropertyValue* valueOf(std::string_view id) const;

  std::string textOf(std::string_view id) const;

  std::expected<void, std::string> setText(std::string_view id, std::string_view text, bool capturing);
  std::expected<void, std::string> setBoolean(std::string_view id, bool value, bool capturing);
  std::expected<void, std::string> setChoice(std::string_view id, uint32_t index, bool capturing);

  std::optional<std::string> errorFor(std::string_view id) const;

  bool isChangedFromDefault(std::string_view id) const;
  bool anyChangedFromDefault() const;

  void resetToDefault(std::string_view id);
  void resetAllToDefault();

  void markBooleansUnknown();
  void endUnknownState();
  bool isStateUnknown(std::string_view id) const;
  bool anyStateUnknown() const;

  std::vector<PropertyValue> valuesToSend() const;

 private:
  std::expected<void, std::string> applyChecked(std::string_view id, const PropertyValue& value, bool capturing);

  std::vector<props::PropertyNode> tree_;
  std::vector<PropertyTreeNode> displayTree_;
  std::map<std::string, PropertyValue, std::less<>> currentValues_;
  std::map<std::string, std::string, std::less<>> errors_;
  std::set<std::string, std::less<>> unknownState_;
};

class DevicePropertiesModel {
 public:
  explicit DevicePropertiesModel(const ipc::DeviceInfo& device);

  const std::vector<std::string>& facetNames() const { return order_; }

  FacetPropertiesModel* facet(std::string_view name);
  const FacetPropertiesModel* facet(std::string_view name) const;

  std::map<std::string, std::vector<PropertyValue>> valuesToSend() const;

 private:
  std::map<std::string, FacetPropertiesModel, std::less<>> facets_;
  std::vector<std::string> order_;
};

}
