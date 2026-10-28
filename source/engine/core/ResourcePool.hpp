#pragma once

#include "core/Assert.hpp"
#include "core/Handle.hpp"

#include <cstdint>
#include <vector>

namespace mengine {

// Fixed capacity pool with a free list, as in Raptor's ResourcePool, plus generations for stale handle checks.
template <typename T, typename HandleT>
class ResourcePool
{
public:
    void Init(uint32_t capacity)
    {
        m_items.assign(capacity, T{});
        m_generations.assign(capacity, 1);
        m_alive.assign(capacity, false);
        m_freeIndices.clear();
        m_freeIndices.reserve(capacity);
        for (uint32_t index = capacity; index > 0; --index)
        {
            m_freeIndices.push_back(index - 1);
        }
        m_usedCount = 0;
    }

    // Returns an invalid handle when the pool is exhausted.
    HandleT Obtain()
    {
        if (m_freeIndices.empty())
        {
            MENGINE_ASSERT(!"ResourcePool exhausted");
            return HandleT{};
        }
        const uint32_t index = m_freeIndices.back();
        m_freeIndices.pop_back();
        m_items[index] = T{};
        m_alive[index] = true;
        ++m_usedCount;
        return HandleT{.index = index, .generation = m_generations[index]};
    }

    void Release(HandleT handle)
    {
        if (!IsAlive(handle))
        {
            return;
        }
        m_items[handle.index] = T{};
        m_alive[handle.index] = false;
        ++m_generations[handle.index];
        m_freeIndices.push_back(handle.index);
        --m_usedCount;
    }

    bool IsAlive(HandleT handle) const
    {
        return handle.index < m_items.size() && m_alive[handle.index] && m_generations[handle.index] == handle.generation;
    }

    T* Access(HandleT handle) { return IsAlive(handle) ? &m_items[handle.index] : nullptr; }
    const T* Access(HandleT handle) const { return IsAlive(handle) ? &m_items[handle.index] : nullptr; }

    uint32_t UsedCount() const { return m_usedCount; }

    template <typename Callback>
    void ForEachAlive(Callback callback)
    {
        for (uint32_t index = 0; index < m_items.size(); ++index)
        {
            if (m_alive[index])
            {
                callback(HandleT{.index = index, .generation = m_generations[index]}, m_items[index]);
            }
        }
    }

private:
    std::vector<T> m_items;
    std::vector<uint32_t> m_generations;
    std::vector<bool> m_alive;
    std::vector<uint32_t> m_freeIndices;
    uint32_t m_usedCount = 0;
};

} // namespace mengine
