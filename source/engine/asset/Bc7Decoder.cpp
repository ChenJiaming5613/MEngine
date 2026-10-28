#include "asset/Bc7Decoder.hpp"

#include <array>
#include <cstring>
#include <utility>

namespace mengine::asset {
namespace {

struct Bc7Mode
{
    uint8_t subsets;
    uint8_t partitionBits;
    uint8_t rotationBits;
    uint8_t indexSelectionBits;
    uint8_t colorBits;
    uint8_t alphaBits;
    uint8_t endpointPBits; // one p-bit per endpoint
    uint8_t sharedPBits;   // one p-bit per subset
    uint8_t indexBits;
    uint8_t secondaryIndexBits;
};

constexpr std::array<Bc7Mode, 8> kBc7Modes = {{
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0},
    {2, 6, 0, 0, 6, 0, 0, 1, 3, 0},
    {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 3},
    {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0},
    {2, 6, 0, 0, 5, 5, 1, 0, 2, 0},
}};

constexpr uint8_t kBc7Partition2[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1}, {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1}, {0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0}, {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0}, {0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0}, {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0}, {0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1}, {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0}, {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0}, {0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1}, {0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
    {0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0}, {0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0}, {0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1},
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1}, {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0}, {0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0}, {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1}, {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0}, {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1},
};

constexpr uint8_t kBc7Partition3[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2}, {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2},
    {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2}, {0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2}, {0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2}, {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0}, {0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0}, {0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1},
    {0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2}, {0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2},
    {0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1}, {0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2}, {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0}, {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0}, {0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1}, {0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1}, {0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1}, {0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1}, {0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2}, {0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1},
    {0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2}, {0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2}, {0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2},
    {0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2},
    {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1}, {0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0},
};

// Anchor pixel of subset 1 (two subsets), and of subsets 1 and 2 (three subsets); subset 0 is anchored at pixel 0.
constexpr uint8_t kBc7Anchor2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15,
};
constexpr uint8_t kBc7Anchor3Second[64] = {
    3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3, 3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15, 3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3,
};
constexpr uint8_t kBc7Anchor3Third[64] = {
    15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8, 15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8, 15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8,
};

constexpr uint8_t kBc7Weights2[4] = {0, 21, 43, 64};
constexpr uint8_t kBc7Weights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr uint8_t kBc7Weights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

uint32_t Bc7Weight(uint32_t indexBits, uint32_t index)
{
    switch (indexBits)
    {
    case 2:
        return kBc7Weights2[index];
    case 3:
        return kBc7Weights3[index];
    default:
        return kBc7Weights4[index];
    }
}

class BlockBitReader
{
public:
    explicit BlockBitReader(const uint8_t* block)
    {
        std::memcpy(&m_low, block, sizeof(m_low));
        std::memcpy(&m_high, block + 8, sizeof(m_high));
    }

    // count <= 8.
    uint32_t Read(uint32_t count)
    {
        if (count == 0)
        {
            return 0;
        }
        uint64_t value;
        if (m_position >= 64)
        {
            value = m_high >> (m_position - 64);
        }
        else if (m_position == 0)
        {
            value = m_low;
        }
        else
        {
            value = (m_low >> m_position) | (m_high << (64 - m_position));
        }
        m_position += count;
        return static_cast<uint32_t>(value & ((uint64_t{1} << count) - 1));
    }

private:
    uint64_t m_low = 0;
    uint64_t m_high = 0;
    uint32_t m_position = 0;
};

uint32_t ExpandEndpoint(uint32_t value, uint32_t precision)
{
    return precision >= 8 ? value : ((value << (8 - precision)) | (value >> (2 * precision - 8)));
}

void DecodeBc7Block(const uint8_t* block, uint8_t pixels[16][4])
{
    BlockBitReader bits(block);
    uint32_t modeIndex = 0;
    while (modeIndex < 8 && bits.Read(1) == 0)
    {
        ++modeIndex;
    }
    if (modeIndex >= 8)
    {
        std::memset(pixels, 0, 16 * 4); // reserved mode: transparent black
        return;
    }
    const Bc7Mode& mode = kBc7Modes[modeIndex];
    const uint32_t partition = bits.Read(mode.partitionBits);
    const uint32_t rotation = bits.Read(mode.rotationBits);
    const uint32_t indexSelection = bits.Read(mode.indexSelectionBits);

    uint32_t endpoints[3][2][4] = {}; // [subset][endpoint][channel]
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        for (uint32_t subset = 0; subset < mode.subsets; ++subset)
        {
            endpoints[subset][0][channel] = bits.Read(mode.colorBits);
            endpoints[subset][1][channel] = bits.Read(mode.colorBits);
        }
    }
    if (mode.alphaBits > 0)
    {
        for (uint32_t subset = 0; subset < mode.subsets; ++subset)
        {
            endpoints[subset][0][3] = bits.Read(mode.alphaBits);
            endpoints[subset][1][3] = bits.Read(mode.alphaBits);
        }
    }

    uint32_t colorPrecision = mode.colorBits;
    uint32_t alphaPrecision = mode.alphaBits;
    const uint32_t pChannels = mode.alphaBits > 0 ? 4u : 3u;
    if (mode.endpointPBits != 0)
    {
        for (uint32_t subset = 0; subset < mode.subsets; ++subset)
        {
            for (uint32_t endpoint = 0; endpoint < 2; ++endpoint)
            {
                const uint32_t pBit = bits.Read(1);
                for (uint32_t channel = 0; channel < pChannels; ++channel)
                {
                    endpoints[subset][endpoint][channel] = (endpoints[subset][endpoint][channel] << 1) | pBit;
                }
            }
        }
        ++colorPrecision;
        alphaPrecision += mode.alphaBits > 0 ? 1u : 0u;
    }
    if (mode.sharedPBits != 0)
    {
        for (uint32_t subset = 0; subset < mode.subsets; ++subset)
        {
            const uint32_t pBit = bits.Read(1);
            for (uint32_t endpoint = 0; endpoint < 2; ++endpoint)
            {
                for (uint32_t channel = 0; channel < pChannels; ++channel)
                {
                    endpoints[subset][endpoint][channel] = (endpoints[subset][endpoint][channel] << 1) | pBit;
                }
            }
        }
        ++colorPrecision;
        alphaPrecision += mode.alphaBits > 0 ? 1u : 0u;
    }
    for (uint32_t subset = 0; subset < mode.subsets; ++subset)
    {
        for (uint32_t endpoint = 0; endpoint < 2; ++endpoint)
        {
            for (uint32_t channel = 0; channel < 3; ++channel)
            {
                endpoints[subset][endpoint][channel] = ExpandEndpoint(endpoints[subset][endpoint][channel], colorPrecision);
            }
            endpoints[subset][endpoint][3] =
                mode.alphaBits > 0 ? ExpandEndpoint(endpoints[subset][endpoint][3], alphaPrecision) : 255u;
        }
    }

    auto subsetOf = [&](uint32_t pixel) -> uint32_t {
        switch (mode.subsets)
        {
        case 2:
            return kBc7Partition2[partition][pixel];
        case 3:
            return kBc7Partition3[partition][pixel];
        default:
            return 0;
        }
    };
    auto isAnchor = [&](uint32_t pixel) {
        if (pixel == 0)
        {
            return true;
        }
        if (mode.subsets == 2)
        {
            return pixel == kBc7Anchor2[partition];
        }
        if (mode.subsets == 3)
        {
            return pixel == kBc7Anchor3Second[partition] || pixel == kBc7Anchor3Third[partition];
        }
        return false;
    };

    uint32_t indices[16];
    uint32_t secondaryIndices[16] = {};
    for (uint32_t pixel = 0; pixel < 16; ++pixel)
    {
        indices[pixel] = bits.Read(mode.indexBits - (isAnchor(pixel) ? 1u : 0u));
    }
    if (mode.secondaryIndexBits != 0)
    {
        for (uint32_t pixel = 0; pixel < 16; ++pixel)
        {
            secondaryIndices[pixel] = bits.Read(mode.secondaryIndexBits - (pixel == 0 ? 1u : 0u));
        }
    }

    for (uint32_t pixel = 0; pixel < 16; ++pixel)
    {
        const uint32_t subset = subsetOf(pixel);
        uint32_t colorWeight;
        uint32_t alphaWeight;
        if (mode.secondaryIndexBits == 0)
        {
            colorWeight = alphaWeight = Bc7Weight(mode.indexBits, indices[pixel]);
        }
        else if (indexSelection == 0)
        {
            colorWeight = Bc7Weight(mode.indexBits, indices[pixel]);
            alphaWeight = Bc7Weight(mode.secondaryIndexBits, secondaryIndices[pixel]);
        }
        else
        {
            colorWeight = Bc7Weight(mode.secondaryIndexBits, secondaryIndices[pixel]);
            alphaWeight = Bc7Weight(mode.indexBits, indices[pixel]);
        }
        const uint32_t* e0 = endpoints[subset][0];
        const uint32_t* e1 = endpoints[subset][1];
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            const uint32_t weight = channel < 3 ? colorWeight : alphaWeight;
            pixels[pixel][channel] = static_cast<uint8_t>(((64 - weight) * e0[channel] + weight * e1[channel] + 32) >> 6);
        }
        if (rotation != 0)
        {
            std::swap(pixels[pixel][3], pixels[pixel][rotation - 1]);
        }
    }
}

} // namespace

std::vector<uint8_t> DecodeBc7(std::span<const uint8_t> blocks, uint32_t width, uint32_t height)
{
    std::vector<uint8_t> rgba(size_t{width} * height * 4, 0);
    const uint32_t blocksX = (width + 3) / 4;
    const uint32_t blocksY = (height + 3) / 4;
    if (blocks.size() < size_t{blocksX} * blocksY * 16)
    {
        return rgba;
    }
    uint8_t pixels[16][4];
    for (uint32_t by = 0; by < blocksY; ++by)
    {
        for (uint32_t bx = 0; bx < blocksX; ++bx)
        {
            DecodeBc7Block(blocks.data() + (size_t{by} * blocksX + bx) * 16, pixels);
            for (uint32_t y = 0; y < 4 && by * 4 + y < height; ++y)
            {
                for (uint32_t x = 0; x < 4 && bx * 4 + x < width; ++x)
                {
                    std::memcpy(&rgba[((size_t{by} * 4 + y) * width + bx * 4 + x) * 4], pixels[y * 4 + x], 4);
                }
            }
        }
    }
    return rgba;
}

} // namespace mengine::asset
