#pragma once

#include "core/Log.hpp"

#include <cstdint>

namespace mengine::render {

// Pipeline variants are created by WarmPipelines before recording; recording only looks them up, so it can run off the
// main thread. A missing variant is a warm-up bug: its draws are skipped and reported once per pass.
inline void ReportMissingPipeline(bool& reported, const char* passName, uint32_t key)
{
    if (!reported)
    {
        reported = true;
        MENGINE_LOG_ERROR("%s: pipeline variant %u was not warmed; skipping its draws", passName, key);
    }
}

} // namespace mengine::render
