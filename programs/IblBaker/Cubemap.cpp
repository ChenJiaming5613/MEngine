#include "Cubemap.hpp"

#include <glm/geometric.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace iblbaker {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct FaceCoordinate
{
    uint32_t face = 0;
    float s = 0.0f;
    float t = 0.0f;
};

// Vulkan spec, "Cube Map Face Selection": major axis, then sc / tc per face.
FaceCoordinate SelectFace(const glm::vec3& r)
{
    const float ax = std::abs(r.x);
    const float ay = std::abs(r.y);
    const float az = std::abs(r.z);
    FaceCoordinate result;
    float sc = 0.0f;
    float tc = 0.0f;
    float ma = 0.0f;
    if (ax >= ay && ax >= az)
    {
        ma = ax;
        if (r.x >= 0.0f)
        {
            result.face = 0;
            sc = -r.z;
            tc = -r.y;
        }
        else
        {
            result.face = 1;
            sc = r.z;
            tc = -r.y;
        }
    }
    else if (ay >= az)
    {
        ma = ay;
        if (r.y >= 0.0f)
        {
            result.face = 2;
            sc = r.x;
            tc = r.z;
        }
        else
        {
            result.face = 3;
            sc = r.x;
            tc = -r.z;
        }
    }
    else
    {
        ma = az;
        if (r.z >= 0.0f)
        {
            result.face = 4;
            sc = r.x;
            tc = -r.y;
        }
        else
        {
            result.face = 5;
            sc = -r.x;
            tc = -r.y;
        }
    }
    result.s = 0.5f * (sc / ma + 1.0f);
    result.t = 0.5f * (tc / ma + 1.0f);
    return result;
}

} // namespace

Cubemap::Cubemap(uint32_t size) : m_size(size)
{
    for (std::vector<glm::vec3>& face : m_faces)
    {
        face.assign(static_cast<size_t>(size) * size, glm::vec3(0.0f));
    }
}

glm::vec3 Cubemap::Sample(const glm::vec3& direction) const
{
    const FaceCoordinate coordinate = SelectFace(direction);
    const float maxCoordinate = static_cast<float>(m_size - 1);
    const float x = std::clamp(coordinate.s * static_cast<float>(m_size) - 0.5f, 0.0f, maxCoordinate);
    const float y = std::clamp(coordinate.t * static_cast<float>(m_size) - 0.5f, 0.0f, maxCoordinate);
    const auto x0 = static_cast<uint32_t>(x);
    const auto y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = std::min(x0 + 1, m_size - 1);
    const uint32_t y1 = std::min(y0 + 1, m_size - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const glm::vec3 top = glm::mix(At(coordinate.face, x0, y0), At(coordinate.face, x1, y0), fx);
    const glm::vec3 bottom = glm::mix(At(coordinate.face, x0, y1), At(coordinate.face, x1, y1), fx);
    return glm::mix(top, bottom, fy);
}

glm::vec3 TexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size)
{
    // Inverse of SelectFace with |ma| = 1.
    const float sc = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
    const float tc = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
    glm::vec3 direction(0.0f);
    switch (face)
    {
    case 0: direction = glm::vec3(1.0f, -tc, -sc); break;
    case 1: direction = glm::vec3(-1.0f, -tc, sc); break;
    case 2: direction = glm::vec3(sc, 1.0f, tc); break;
    case 3: direction = glm::vec3(sc, -1.0f, -tc); break;
    case 4: direction = glm::vec3(sc, -tc, 1.0f); break;
    default: direction = glm::vec3(-sc, -tc, -1.0f); break;
    }
    return glm::normalize(direction);
}

Cubemap EquirectToCube(const mengine::asset::ImageFloat& image, uint32_t size, uint32_t threads)
{
    Cubemap cube(size);
    const auto width = static_cast<int64_t>(image.width);
    const auto height = static_cast<int64_t>(image.height);
    auto texel = [&](int64_t x, int64_t y) {
        x = ((x % width) + width) % width;
        y = std::clamp<int64_t>(y, 0, height - 1);
        const float* p = &image.rgba[(static_cast<size_t>(y) * image.width + static_cast<size_t>(x)) * 4];
        return glm::vec3(p[0], p[1], p[2]);
    };

    ForEachCubeRow(size, threads, [&](uint32_t face, uint32_t y) {
        for (uint32_t x = 0; x < size; ++x)
        {
            const glm::vec3 d = TexelDirection(face, x, y, size);
            const float u = 0.5f + std::atan2(d.x, -d.z) / (2.0f * kPi);
            const float v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / kPi;
            const float px = u * static_cast<float>(width) - 0.5f;
            const float py = v * static_cast<float>(height) - 0.5f;
            const auto x0 = static_cast<int64_t>(std::floor(px));
            const auto y0 = static_cast<int64_t>(std::floor(py));
            const float fx = px - static_cast<float>(x0);
            const float fy = py - static_cast<float>(y0);
            const glm::vec3 top = glm::mix(texel(x0, y0), texel(x0 + 1, y0), fx);
            const glm::vec3 bottom = glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), fx);
            cube.At(face, x, y) = glm::mix(top, bottom, fy);
        }
    });
    return cube;
}

Cubemap ConstantCube(uint32_t size, const glm::vec3& value)
{
    Cubemap cube(size);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                cube.At(face, x, y) = value;
            }
        }
    }
    return cube;
}

Cubemap MirrorY(const Cubemap& source)
{
    // TexelDirection: negating y maps (face, x, y) to (face with +Y / -Y swapped, x, size - 1 - y).
    constexpr std::array<uint32_t, 6> kMirroredFace = {0, 1, 3, 2, 4, 5};
    const uint32_t size = source.Size();
    Cubemap mirrored(size);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                mirrored.At(face, x, y) = source.At(kMirroredFace[face], x, size - 1 - y);
            }
        }
    }
    return mirrored;
}

Cubemap ResampleCube(const Cubemap& source, uint32_t size, uint32_t threads)
{
    Cubemap cube(size);
    ForEachCubeRow(size, threads, [&](uint32_t face, uint32_t y) {
        for (uint32_t x = 0; x < size; ++x)
        {
            cube.At(face, x, y) = source.Sample(TexelDirection(face, x, y, size));
        }
    });
    return cube;
}

void ParallelFor(uint32_t count, uint32_t threads, const std::function<void(uint32_t index)>& function)
{
    threads = std::max(1u, std::min(threads, count));
    std::atomic<uint32_t> next{0};
    auto worker = [&]() {
        for (uint32_t index = next.fetch_add(1); index < count; index = next.fetch_add(1))
        {
            function(index);
        }
    };
    std::vector<std::thread> workers;
    for (uint32_t i = 1; i < threads; ++i)
    {
        workers.emplace_back(worker);
    }
    worker();
    for (std::thread& thread : workers)
    {
        thread.join();
    }
}

void ForEachCubeRow(uint32_t size, uint32_t threads, const std::function<void(uint32_t face, uint32_t y)>& rowFunction)
{
    ParallelFor(6 * size, threads, [&](uint32_t index) { rowFunction(index / size, index % size); });
}

} // namespace iblbaker
