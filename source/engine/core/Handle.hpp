#pragma once

#include <cstdint>
#include <functional>

namespace mengine {

// Typed index into a ResourcePool. The generation detects use after release.
template <typename Tag>
struct Handle
{
    static constexpr uint32_t kInvalidIndex = 0xffffffffu;

    uint32_t index = kInvalidIndex;
    uint32_t generation = 0;

    bool IsValid() const { return index != kInvalidIndex; }

    friend bool operator==(const Handle&, const Handle&) = default;
};

} // namespace mengine

template <typename Tag>
struct std::hash<mengine::Handle<Tag>>
{
    size_t operator()(const mengine::Handle<Tag>& handle) const noexcept
    {
        return std::hash<uint64_t>()((static_cast<uint64_t>(handle.generation) << 32) | handle.index);
    }
};
