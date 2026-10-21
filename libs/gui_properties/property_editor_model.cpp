/*
 * SPDX-License-Identifier: MIT
 */

#include "property_editor_model.h"

#include "display_text.h"

#include <type_traits>
#include <utility>
#include <variant>

#include "device_properties.h"

namespace bitoscope::gui_properties {

namespace dp = bitoscope::device_properties;

bool PropertyGroupDisplay::operator==(const PropertyGroupDisplay& other) const {
  return name == other.name && children == other.children;
}

namespace {

std::optional<std::string> boundText(const props::Property& property, const props::Bound& bound) {
  const auto* v = std::get_if<int64_t>(&bound);
  if (v == nullptr) return std::nullopt;
  auto formatted = dp::formatValue(property, PropertyValue{property.id, *v});
  if (!formatted.has_value()) return std::nullopt;
  return *formatted;
}

PropertyDisplay describeProperty(const props::Property& property) {
  PropertyDisplay d;
  d.id = property.id;
  // WHY: Name and help are converted here: a driver's own text arrives unconverted. Ids and enumeration values
  // stay exactly as typed.
  d.label = display_text::displayText(property.name);
  d.help = display_text::displayText(property.help);
  d.changeableDuringCapture = property.changeableDuringCapture;
  d.unitSymbol = std::string(dp::unitSymbol(property.unit));

  std::visit(
      [&](const auto& spec) {
        using T = std::decay_t<decltype(spec)>;
        if constexpr (std::is_same_v<T, props::IntegerSpec>) {
          d.kind = PropertyKind::Integer;
          d.minText = boundText(property, spec.min);
          d.maxText = boundText(property, spec.max);
        } else if constexpr (std::is_same_v<T, props::DecimalSpec>) {
          d.kind = PropertyKind::Decimal;
          d.minText = boundText(property, spec.min);
          d.maxText = boundText(property, spec.max);
        } else if constexpr (std::is_same_v<T, props::BooleanSpec>) {
          d.kind = PropertyKind::Boolean;
        } else if constexpr (std::is_same_v<T, props::EnumerationSpec>) {
          d.kind = PropertyKind::Enumeration;
          d.choices.reserve(spec.items.size());
          for (size_t i = 0; i < spec.items.size(); ++i) {
            auto text = dp::formatValue(property, PropertyValue{property.id, props::EnumIndex{static_cast<uint32_t>(i)}});
            d.choices.push_back(EnumerationChoice{text.value_or(std::string("(unknown)"))});
          }
        } else {
          d.kind = PropertyKind::Unknown;
        }
      },
      property.type);

  if (auto def = dp::effectiveDefault(property); def.has_value()) {
    auto text = dp::formatValue(property, *def);
    d.defaultText = text.value_or(std::string());
  }
  return d;
}

PropertyDisplay unknownNodePlaceholder() {
  PropertyDisplay d;
  d.label = "(unknown property)";
  d.kind = PropertyKind::Unknown;
  return d;
}

PropertyTreeNode describeNode(const props::PropertyNode& node) {
  if (const auto* group = std::get_if<props::PropertyGroup>(&node.node)) {
    PropertyGroupDisplay g;
    g.name = display_text::displayText(group->name);
    g.children.reserve(group->nodes.size());
    for (const auto& child : group->nodes) g.children.push_back(describeNode(child));
    return PropertyTreeNode{std::move(g)};
  }
  if (const auto* property = std::get_if<props::Property>(&node.node)) {
    return PropertyTreeNode{describeProperty(*property)};
  }
  return PropertyTreeNode{unknownNodePlaceholder()};
}

void collectFlat(const std::vector<PropertyTreeNode>& tree, std::vector<const PropertyDisplay*>& out) {
  for (const PropertyTreeNode& n : tree) {
    if (const auto* p = std::get_if<PropertyDisplay>(&n.node)) {
      out.push_back(p);
    } else if (const auto* g = std::get_if<PropertyGroupDisplay>(&n.node)) {
      collectFlat(g->children, out);
    }
  }
}

}

std::vector<PropertyTreeNode> buildPropertyTree(const std::vector<props::PropertyNode>& nodes) {
  std::vector<PropertyTreeNode> out;
  out.reserve(nodes.size());
  for (const props::PropertyNode& n : nodes) out.push_back(describeNode(n));
  return out;
}

std::vector<const PropertyDisplay*> flattenProperties(const std::vector<PropertyTreeNode>& tree) {
  std::vector<const PropertyDisplay*> out;
  collectFlat(tree, out);
  return out;
}

FacetPropertiesModel::FacetPropertiesModel(std::vector<props::PropertyNode> tree)
    : tree_(std::move(tree)), displayTree_(buildPropertyTree(tree_)) {
  for (const dp::PropertyListing& listing : dp::listProperties(tree_)) {
    if (auto def = dp::effectiveDefault(*listing.property); def.has_value()) {
      currentValues_.emplace(listing.property->id, std::move(*def));
    }
  }
}

const PropertyValue* FacetPropertiesModel::valueOf(std::string_view id) const {
  const auto it = currentValues_.find(id);
  return it == currentValues_.end() ? nullptr : &it->second;
}

std::string FacetPropertiesModel::textOf(std::string_view id) const {
  const props::Property* property = dp::findProperty(tree_, id);
  const PropertyValue* value = valueOf(id);
  if (property == nullptr || value == nullptr) return "";
  auto formatted = dp::formatValue(*property, *value);
  return formatted.value_or(std::string());
}

std::expected<void, std::string> FacetPropertiesModel::applyChecked(std::string_view id, const PropertyValue& value,
                                                                     bool capturing) {
  const props::Property* property = dp::findProperty(tree_, id);
  if (property == nullptr) {
    return std::unexpected(std::string("no property has id '") + std::string(id) + "'");
  }
  auto checked = dp::checkValue(*property, value, capturing);
  if (!checked.has_value()) {
    errors_[std::string(id)] = checked.error();
    return std::unexpected(checked.error());
  }
  currentValues_.insert_or_assign(std::string(id), value);
  errors_.erase(std::string(id));
  unknownState_.erase(std::string(id));
  return {};
}

std::expected<void, std::string> FacetPropertiesModel::setText(std::string_view id, std::string_view text,
                                                                bool capturing) {
  const props::Property* property = dp::findProperty(tree_, id);
  if (property == nullptr) {
    return std::unexpected(std::string("no property has id '") + std::string(id) + "'");
  }
  auto parsed = dp::parseValue(*property, text);
  if (!parsed.has_value()) {
    errors_[std::string(id)] = parsed.error();
    return std::unexpected(parsed.error());
  }
  return applyChecked(id, *parsed, capturing);
}

std::expected<void, std::string> FacetPropertiesModel::setBoolean(std::string_view id, bool value, bool capturing) {
  return applyChecked(id, PropertyValue{std::string(id), value}, capturing);
}

std::expected<void, std::string> FacetPropertiesModel::setChoice(std::string_view id, uint32_t index,
                                                                  bool capturing) {
  return applyChecked(id, PropertyValue{std::string(id), props::EnumIndex{index}}, capturing);
}

std::optional<std::string> FacetPropertiesModel::errorFor(std::string_view id) const {
  const auto it = errors_.find(id);
  return it == errors_.end() ? std::nullopt : std::optional<std::string>(it->second);
}

bool FacetPropertiesModel::isChangedFromDefault(std::string_view id) const {
  const props::Property* property = dp::findProperty(tree_, id);
  const PropertyValue* value = valueOf(id);
  if (property == nullptr || value == nullptr) return false;
  auto def = dp::effectiveDefault(*property);
  if (!def.has_value()) return false;
  return !(*value == *def);
}

bool FacetPropertiesModel::anyChangedFromDefault() const {
  for (const dp::PropertyListing& listing : dp::listProperties(tree_)) {
    if (isChangedFromDefault(listing.property->id)) return true;
  }
  return false;
}

void FacetPropertiesModel::resetToDefault(std::string_view id) {
  const props::Property* property = dp::findProperty(tree_, id);
  if (property == nullptr) return;
  auto def = dp::effectiveDefault(*property);
  if (!def.has_value()) return;
  currentValues_.insert_or_assign(property->id, *def);
  errors_.erase(property->id);
  unknownState_.erase(property->id);
}

void FacetPropertiesModel::resetAllToDefault() {
  for (const dp::PropertyListing& listing : dp::listProperties(tree_)) {
    resetToDefault(listing.property->id);
  }
}

void FacetPropertiesModel::markBooleansUnknown() {
  for (const dp::PropertyListing& listing : dp::listProperties(tree_)) {
    if (std::holds_alternative<props::BooleanSpec>(listing.property->type)) {
      unknownState_.insert(listing.property->id);
    }
  }
}

void FacetPropertiesModel::endUnknownState() { unknownState_.clear(); }

bool FacetPropertiesModel::isStateUnknown(std::string_view id) const { return unknownState_.contains(id); }

bool FacetPropertiesModel::anyStateUnknown() const { return !unknownState_.empty(); }

std::vector<PropertyValue> FacetPropertiesModel::valuesToSend() const {
  std::vector<PropertyValue> out;
  for (const dp::PropertyListing& listing : dp::listProperties(tree_)) {
    if (const PropertyValue* v = valueOf(listing.property->id); v != nullptr) {
      out.push_back(*v);
    }
  }
  return out;
}

DevicePropertiesModel::DevicePropertiesModel(const ipc::DeviceInfo& device) {
  for (const ipc::FacetInfo& facet : device.facets) {
    order_.push_back(facet.name);
    facets_.emplace(facet.name, FacetPropertiesModel(facet.properties));
  }
}

FacetPropertiesModel* DevicePropertiesModel::facet(std::string_view name) {
  const auto it = facets_.find(name);
  return it == facets_.end() ? nullptr : &it->second;
}

const FacetPropertiesModel* DevicePropertiesModel::facet(std::string_view name) const {
  const auto it = facets_.find(name);
  return it == facets_.end() ? nullptr : &it->second;
}

std::map<std::string, std::vector<PropertyValue>> DevicePropertiesModel::valuesToSend() const {
  std::map<std::string, std::vector<PropertyValue>> out;
  for (const auto& name : order_) {
    const FacetPropertiesModel& model = facets_.at(name);
    if (auto values = model.valuesToSend(); !values.empty()) {
      out.emplace(name, std::move(values));
    }
  }
  return out;
}

}
