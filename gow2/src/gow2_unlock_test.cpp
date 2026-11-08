// Experimental, in-memory campaign selector patches. This file is linked only into gow2-unlock-test.

#include "gow2_pch.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>

namespace {

constexpr uint32_t kGNames = 0x83136834;
constexpr uint32_t kGObjObjects = 0x83136860;
constexpr uint32_t kObjectOuter = 40;
constexpr uint32_t kObjectNameIndex = 44;
constexpr uint32_t kObjectNameNumber = 48;
constexpr uint32_t kObjectClass = 52;
constexpr uint32_t kStructScript = 84;
constexpr uint32_t kArrayData = 0;
constexpr uint32_t kArrayNum = 4;
constexpr uint32_t kArrayMax = 8;
constexpr uint32_t kNameEntryText = 8;
constexpr uint32_t kNameWideBit = 1;
constexpr uint32_t kMaxTableCount = 1u << 24;

struct GuestArray {
  uint32_t data;
  uint32_t num;
  uint32_t max;
};

struct ByteEdit {
  uint16_t offset;
  uint8_t original;
  uint8_t patched;
};

struct ScriptPatch {
  std::string_view path;
  uint32_t script_size;
  std::array<ByteEdit, 2> edits;
  uint32_t edit_count;
};

constexpr std::array<ScriptPatch, 4> kPatches = {{
    {"GearGame.GearUISceneFE_SelectAct.CanAccessAct",
     110,
     {{{0x001D, 0x07, 0x06}, {0x0066, 0x28, 0x27}}},
     2},
    {"GearGame.GearUISceneFE_SelectChapter.CanAccessChapter",
     145,
     {{{0x0042, 0x07, 0x06}, {0x0089, 0x28, 0x27}}},
     2},
    {"GearGame.GearUISceneFE_SelectAct.SetDifficultyImage",
     497,
     {{{0x0120, 0x07, 0x06}, {0, 0, 0}}},
     1},
    {"GearGame.GearUISceneFE_SelectChapter.SetDifficultyImage",
     540,
     {{{0x014D, 0x07, 0x06}, {0, 0, 0}}},
     1},
}};

GuestArray ReadArray(uint8_t* base, uint32_t address) {
  return {REX_LOAD_U32(address + kArrayData), REX_LOAD_U32(address + kArrayNum),
          REX_LOAD_U32(address + kArrayMax)};
}

bool Plausible(const GuestArray& array) {
  return array.data != 0 && array.num != 0 && array.num <= kMaxTableCount && array.max >= array.num;
}

uint32_t FoldAscii(uint32_t value) {
  return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

class GuestObjects {
 public:
  explicit GuestObjects(uint8_t* base)
      : base(base), names_(ReadArray(base, kGNames)), objects_(ReadArray(base, kGObjObjects)) {}

  bool Readable() const { return Plausible(names_) && Plausible(objects_); }

  std::optional<uint32_t> FindFunction(std::string_view path) {
    std::vector<uint32_t> path_names;
    for (size_t start = 0; start <= path.size();) {
      const size_t dot = path.find('.', start);
      const size_t end = dot == std::string_view::npos ? path.size() : dot;
      const auto name = NameIndex(path.substr(start, end - start));
      if (!name) return std::nullopt;
      path_names.push_back(*name);
      if (dot == std::string_view::npos) break;
      start = dot + 1;
    }

    const auto function_class = NameIndex("Function");
    if (!function_class || path_names.empty()) return std::nullopt;

    std::optional<uint32_t> found;
    for (uint32_t i = 0; i < objects_.num; ++i) {
      const uint32_t object = REX_LOAD_U32(objects_.data + i * 4);
      if (!object || !ClassIs(object, *function_class) || !PathIs(object, path_names)) continue;
      if (found) return std::nullopt;
      found = object;
    }
    return found;
  }

 private:
  bool NameEntryIs(uint32_t entry, std::string_view text) const {
    const bool wide = (REX_LOAD_U32(entry) & kNameWideBit) != 0;
    const uint32_t width = wide ? 2 : 1;
    const uint32_t start = entry + kNameEntryText;
    for (size_t i = 0; i <= text.size(); ++i) {
      const uint32_t address = start + uint32_t(i) * width;
      const uint32_t value = wide ? REX_LOAD_U16(address) : REX_LOAD_U8(address);
      if (i == text.size()) return value == 0;
      if (value == 0 || FoldAscii(value) != FoldAscii(static_cast<uint8_t>(text[i]))) return false;
    }
    return false;
  }

  std::optional<uint32_t> NameIndex(std::string_view text) {
    const std::string key(text);
    if (const auto it = name_cache_.find(key); it != name_cache_.end()) return it->second;

    std::optional<uint32_t> found;
    for (uint32_t i = 0; i < names_.num; ++i) {
      const uint32_t entry = REX_LOAD_U32(names_.data + i * 4);
      if (!entry || !NameEntryIs(entry, text)) continue;
      if (found) {
        name_cache_.emplace(key, std::nullopt);
        return std::nullopt;
      }
      found = i;
    }
    name_cache_.emplace(key, found);
    return found;
  }

  bool NameIs(uint32_t object, uint32_t index) const {
    return REX_LOAD_U32(object + kObjectNameIndex) == index &&
           REX_LOAD_U32(object + kObjectNameNumber) == 0;
  }

  bool ClassIs(uint32_t object, uint32_t class_name) const {
    const uint32_t cls = REX_LOAD_U32(object + kObjectClass);
    return cls != 0 && NameIs(cls, class_name);
  }

  bool PathIs(uint32_t object, const std::vector<uint32_t>& path) const {
    uint32_t current = object;
    for (size_t i = path.size(); i-- > 0;) {
      if (!current || !NameIs(current, path[i])) return false;
      current = REX_LOAD_U32(current + kObjectOuter);
    }
    return current == 0;
  }

  GuestArray names_;
  GuestArray objects_;
  std::unordered_map<std::string, std::optional<uint32_t>> name_cache_;
  uint8_t* base;
};

bool PlanPatches(uint8_t* base, std::vector<std::pair<uint32_t, uint8_t>>& writes, std::string& reason) {
  GuestObjects objects(base);
  if (!objects.Readable()) {
    reason = "name or object table is unreadable";
    return false;
  }

  for (const ScriptPatch& patch : kPatches) {
    const auto function = objects.FindFunction(patch.path);
    if (!function) {
      reason = std::string(patch.path) + " was not found uniquely";
      return false;
    }

    const GuestArray script = ReadArray(base, *function + kStructScript);
    if (!script.data || script.num != patch.script_size || script.max < script.num) {
      reason = std::string(patch.path) + " has an unexpected script array";
      return false;
    }

    for (uint32_t i = 0; i < patch.edit_count; ++i) {
      const ByteEdit& edit = patch.edits[i];
      const uint8_t value = REX_LOAD_U8(script.data + edit.offset);
      if (value != edit.original && value != edit.patched) {
        reason = fmt::format("{} +0x{:04X}: expected {:02X} or {:02X}, found {:02X}", patch.path,
                             edit.offset, edit.original, edit.patched, value);
        return false;
      }
      if (value == edit.original) writes.emplace_back(script.data + edit.offset, edit.patched);
    }

    // Validate the fixed jump destination and the return opcode/constant as a second guard against
    // applying these byte edits to a different retail script layout.
    if (patch.path == "GearGame.GearUISceneFE_SelectAct.CanAccessAct") {
      if (REX_LOAD_U8(script.data + 0x001E) != 0x00 ||
          REX_LOAD_U8(script.data + 0x001F) != 0x65 ||
          REX_LOAD_U8(script.data + 0x0065) != 0x04) {
        reason = "CanAccessAct bytecode context does not match";
        return false;
      }
    } else if (patch.path == "GearGame.GearUISceneFE_SelectChapter.CanAccessChapter") {
      if (REX_LOAD_U8(script.data + 0x0043) != 0x00 ||
          REX_LOAD_U8(script.data + 0x0044) != 0x88 ||
          REX_LOAD_U8(script.data + 0x0088) != 0x04) {
        reason = "CanAccessChapter bytecode context does not match";
        return false;
      }
    } else if (patch.path == "GearGame.GearUISceneFE_SelectAct.SetDifficultyImage") {
      if (REX_LOAD_U8(script.data + 0x0121) != 0x01 ||
          REX_LOAD_U8(script.data + 0x0122) != 0x8A) {
        reason = "Act lock-icon bytecode context does not match";
        return false;
      }
    } else if (patch.path == "GearGame.GearUISceneFE_SelectChapter.SetDifficultyImage") {
      if (REX_LOAD_U8(script.data + 0x014E) != 0x01 ||
          REX_LOAD_U8(script.data + 0x014F) != 0xB5) {
        reason = "Chapter lock-icon bytecode context does not match";
        return false;
      }
    }
  }
  return true;
}

void ApplySelectorUnlocks(uint8_t* base) {
  std::vector<std::pair<uint32_t, uint8_t>> writes;
  std::string reason;
  if (!PlanPatches(base, writes, reason)) {
    REXLOG_ERROR("[unlock-test] selector patch skipped: {}", reason);
    return;
  }
  for (const auto& [address, value] : writes) REX_STORE_U8(address, value);
  REXLOG_INFO("[unlock-test] campaign selector patches applied ({} byte writes; profile and saves unchanged)",
              writes.size());
}

}  // namespace

REX_EXTERN(__imp__sub_8283F780);

REX_HOOK_RAW(sub_8283F780) {
  __imp__sub_8283F780(ctx, base);
  ApplySelectorUnlocks(base);
}
