// Ported from NVIDIA Donut, src/engine/DDSFile.cpp
//   https://github.com/NVIDIA-RTX/Donut/blob/2c1077673edb0e0d814c202e6ce8e502d245c2da/src/engine/DDSFile.cpp
// Differences from upstream: VkFormat instead of nvrhi::Format (as nvrhi's Vulkan backend maps them); the Donut / nvrhi
// types come from DdsFile.hpp and TextureData::data is a span; ConvertDDSFormat's inverted forceSRGB conditions for
// 32 bit RGBA / BGRA are fixed; texture creation and DDS saving are dropped (GltfLoader creates the texture).

/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// This file is loosely based on DDSTextureLoader.cpp from Microsoft DirectXTK library.

//--------------------------------------------------------------------------------------
// File: DDSTextureLoader.cpp
//
// Functions for loading a DDS texture and creating a Direct3D runtime resource for it
//
// Note these functions are useful as a light-weight runtime loader for DDS files. For
// a full-featured DDS file reader, writer, and texture processing pipeline see
// the 'Texconv' sample and the 'DirectXTex' library.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=248926
// http://go.microsoft.com/fwlink/?LinkId=248929
//--------------------------------------------------------------------------------------

/*
    The MIT License (MIT)

    Copyright (c) 2018 Microsoft Corp

    Permission is hereby granted, free of charge, to any person obtaining a copy of this
    software and associated documentation files (the "Software"), to deal in the Software
    without restriction, including without limitation the rights to use, copy, modify,
    merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to the following
    conditions:

    The above copyright notice and this permission notice shall be included in all copies
    or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
    INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
    PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
    OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "render/DdsFile.hpp"

#include "render/DdsHeader.hpp"

#include <algorithm>
#include <iterator>

#define D3D11_RESOURCE_MISC_TEXTURECUBE 0x4

using namespace mengine::render::dds;

namespace mengine::render
{

    struct FormatMapping
    {
        VkFormat vkFormat;
        DXGI_FORMAT dxgiFormat;
        uint32_t bitsPerPixel;
    };

    // nvrhi name in the trailing comment. Lookups by DXGI format take the first match.
    const FormatMapping g_FormatMappings[] = {
        { VK_FORMAT_UNDEFINED,                DXGI_FORMAT_UNKNOWN,                0 },   // UNKNOWN
        { VK_FORMAT_R8_UINT,                  DXGI_FORMAT_R8_UINT,                8 },   // R8_UINT
        { VK_FORMAT_R8_SINT,                  DXGI_FORMAT_R8_SINT,                8 },   // R8_SINT
        { VK_FORMAT_R8_UNORM,                 DXGI_FORMAT_R8_UNORM,               8 },   // R8_UNORM
        { VK_FORMAT_R8_SNORM,                 DXGI_FORMAT_R8_SNORM,               8 },   // R8_SNORM
        { VK_FORMAT_R8G8_UINT,                DXGI_FORMAT_R8G8_UINT,              16 },  // RG8_UINT
        { VK_FORMAT_R8G8_SINT,                DXGI_FORMAT_R8G8_SINT,              16 },  // RG8_SINT
        { VK_FORMAT_R8G8_UNORM,               DXGI_FORMAT_R8G8_UNORM,             16 },  // RG8_UNORM
        { VK_FORMAT_R8G8_SNORM,               DXGI_FORMAT_R8G8_SNORM,             16 },  // RG8_SNORM
        { VK_FORMAT_R16_UINT,                 DXGI_FORMAT_R16_UINT,               16 },  // R16_UINT
        { VK_FORMAT_R16_SINT,                 DXGI_FORMAT_R16_SINT,               16 },  // R16_SINT
        { VK_FORMAT_R16_UNORM,                DXGI_FORMAT_R16_UNORM,              16 },  // R16_UNORM
        { VK_FORMAT_R16_SNORM,                DXGI_FORMAT_R16_SNORM,              16 },  // R16_SNORM
        { VK_FORMAT_R16_SFLOAT,               DXGI_FORMAT_R16_FLOAT,              16 },  // R16_FLOAT
        { VK_FORMAT_B4G4R4A4_UNORM_PACK16,    DXGI_FORMAT_B4G4R4A4_UNORM,         16 },  // BGRA4_UNORM
        { VK_FORMAT_B5G6R5_UNORM_PACK16,      DXGI_FORMAT_B5G6R5_UNORM,           16 },  // B5G6R5_UNORM
        { VK_FORMAT_B5G5R5A1_UNORM_PACK16,    DXGI_FORMAT_B5G5R5A1_UNORM,         16 },  // B5G5R5A1_UNORM
        { VK_FORMAT_R8G8B8A8_UINT,            DXGI_FORMAT_R8G8B8A8_UINT,          32 },  // RGBA8_UINT
        { VK_FORMAT_R8G8B8A8_SINT,            DXGI_FORMAT_R8G8B8A8_SINT,          32 },  // RGBA8_SINT
        { VK_FORMAT_R8G8B8A8_UNORM,           DXGI_FORMAT_R8G8B8A8_UNORM,         32 },  // RGBA8_UNORM
        { VK_FORMAT_R8G8B8A8_SNORM,           DXGI_FORMAT_R8G8B8A8_SNORM,         32 },  // RGBA8_SNORM
        { VK_FORMAT_B8G8R8A8_UNORM,           DXGI_FORMAT_B8G8R8A8_UNORM,         32 },  // BGRA8_UNORM
        { VK_FORMAT_B8G8R8A8_UNORM,           DXGI_FORMAT_B8G8R8X8_UNORM,         32 },  // BGRX8_UNORM
        { VK_FORMAT_R8G8B8A8_SRGB,            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,    32 },  // SRGBA8_UNORM
        { VK_FORMAT_B8G8R8A8_SRGB,            DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,    32 },  // SBGRA8_UNORM
        { VK_FORMAT_B8G8R8A8_SRGB,            DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,    32 },  // SBGRX8_UNORM
        { VK_FORMAT_A2B10G10R10_UNORM_PACK32, DXGI_FORMAT_R10G10B10A2_UNORM,      32 },  // R10G10B10A2_UNORM
        { VK_FORMAT_B10G11R11_UFLOAT_PACK32,  DXGI_FORMAT_R11G11B10_FLOAT,        32 },  // R11G11B10_FLOAT
        { VK_FORMAT_R16G16_UINT,              DXGI_FORMAT_R16G16_UINT,            32 },  // RG16_UINT
        { VK_FORMAT_R16G16_SINT,              DXGI_FORMAT_R16G16_SINT,            32 },  // RG16_SINT
        { VK_FORMAT_R16G16_UNORM,             DXGI_FORMAT_R16G16_UNORM,           32 },  // RG16_UNORM
        { VK_FORMAT_R16G16_SNORM,             DXGI_FORMAT_R16G16_SNORM,           32 },  // RG16_SNORM
        { VK_FORMAT_R16G16_SFLOAT,            DXGI_FORMAT_R16G16_FLOAT,           32 },  // RG16_FLOAT
        { VK_FORMAT_R32_UINT,                 DXGI_FORMAT_R32_UINT,               32 },  // R32_UINT
        { VK_FORMAT_R32_SINT,                 DXGI_FORMAT_R32_SINT,               32 },  // R32_SINT
        { VK_FORMAT_R32_SFLOAT,               DXGI_FORMAT_R32_FLOAT,              32 },  // R32_FLOAT
        { VK_FORMAT_R16G16B16A16_UINT,        DXGI_FORMAT_R16G16B16A16_UINT,      64 },  // RGBA16_UINT
        { VK_FORMAT_R16G16B16A16_SINT,        DXGI_FORMAT_R16G16B16A16_SINT,      64 },  // RGBA16_SINT
        { VK_FORMAT_R16G16B16A16_SFLOAT,      DXGI_FORMAT_R16G16B16A16_FLOAT,     64 },  // RGBA16_FLOAT
        { VK_FORMAT_R16G16B16A16_UNORM,       DXGI_FORMAT_R16G16B16A16_UNORM,     64 },  // RGBA16_UNORM
        { VK_FORMAT_R16G16B16A16_SNORM,       DXGI_FORMAT_R16G16B16A16_SNORM,     64 },  // RGBA16_SNORM
        { VK_FORMAT_R32G32_UINT,              DXGI_FORMAT_R32G32_UINT,            64 },  // RG32_UINT
        { VK_FORMAT_R32G32_SINT,              DXGI_FORMAT_R32G32_SINT,            64 },  // RG32_SINT
        { VK_FORMAT_R32G32_SFLOAT,            DXGI_FORMAT_R32G32_FLOAT,           64 },  // RG32_FLOAT
        { VK_FORMAT_R32G32B32_UINT,           DXGI_FORMAT_R32G32B32_UINT,         96 },  // RGB32_UINT
        { VK_FORMAT_R32G32B32_SINT,           DXGI_FORMAT_R32G32B32_SINT,         96 },  // RGB32_SINT
        { VK_FORMAT_R32G32B32_SFLOAT,         DXGI_FORMAT_R32G32B32_FLOAT,        96 },  // RGB32_FLOAT
        { VK_FORMAT_R32G32B32A32_UINT,        DXGI_FORMAT_R32G32B32A32_UINT,      128 }, // RGBA32_UINT
        { VK_FORMAT_R32G32B32A32_SINT,        DXGI_FORMAT_R32G32B32A32_SINT,      128 }, // RGBA32_SINT
        { VK_FORMAT_R32G32B32A32_SFLOAT,      DXGI_FORMAT_R32G32B32A32_FLOAT,     128 }, // RGBA32_FLOAT
        { VK_FORMAT_D16_UNORM,                DXGI_FORMAT_R16_UNORM,              16 },  // D16
        { VK_FORMAT_D24_UNORM_S8_UINT,        DXGI_FORMAT_R24_UNORM_X8_TYPELESS,  32 },  // D24S8
        { VK_FORMAT_D24_UNORM_S8_UINT,        DXGI_FORMAT_X24_TYPELESS_G8_UINT,   32 },  // X24G8_UINT
        { VK_FORMAT_D32_SFLOAT,               DXGI_FORMAT_R32_FLOAT,              32 },  // D32
        { VK_FORMAT_D32_SFLOAT_S8_UINT,       DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, 64 }, // D32S8
        { VK_FORMAT_D32_SFLOAT_S8_UINT,       DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,  64 }, // X32G8_UINT
        { VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     DXGI_FORMAT_BC1_UNORM,              4 },   // BC1_UNORM
        { VK_FORMAT_BC1_RGBA_SRGB_BLOCK,      DXGI_FORMAT_BC1_UNORM_SRGB,         4 },   // BC1_UNORM_SRGB
        { VK_FORMAT_BC2_UNORM_BLOCK,          DXGI_FORMAT_BC2_UNORM,              8 },   // BC2_UNORM
        { VK_FORMAT_BC2_SRGB_BLOCK,           DXGI_FORMAT_BC2_UNORM_SRGB,         8 },   // BC2_UNORM_SRGB
        { VK_FORMAT_BC3_UNORM_BLOCK,          DXGI_FORMAT_BC3_UNORM,              8 },   // BC3_UNORM
        { VK_FORMAT_BC3_SRGB_BLOCK,           DXGI_FORMAT_BC3_UNORM_SRGB,         8 },   // BC3_UNORM_SRGB
        { VK_FORMAT_BC4_UNORM_BLOCK,          DXGI_FORMAT_BC4_UNORM,              4 },   // BC4_UNORM
        { VK_FORMAT_BC4_SNORM_BLOCK,          DXGI_FORMAT_BC4_SNORM,              4 },   // BC4_SNORM
        { VK_FORMAT_BC5_UNORM_BLOCK,          DXGI_FORMAT_BC5_UNORM,              8 },   // BC5_UNORM
        { VK_FORMAT_BC5_SNORM_BLOCK,          DXGI_FORMAT_BC5_SNORM,              8 },   // BC5_SNORM
        { VK_FORMAT_BC6H_UFLOAT_BLOCK,        DXGI_FORMAT_BC6H_UF16,              8 },   // BC6H_UFLOAT
        { VK_FORMAT_BC6H_SFLOAT_BLOCK,        DXGI_FORMAT_BC6H_SF16,              8 },   // BC6H_SFLOAT
        { VK_FORMAT_BC7_UNORM_BLOCK,          DXGI_FORMAT_BC7_UNORM,              8 },   // BC7_UNORM
        { VK_FORMAT_BC7_SRGB_BLOCK,           DXGI_FORMAT_BC7_UNORM_SRGB,         8 },   // BC7_UNORM_SRGB
    };

#define ISBITMASK( r,g,b,a ) ( ddpf.RBitMask == r && ddpf.GBitMask == g && ddpf.BBitMask == b && ddpf.ABitMask == a )

    static VkFormat ConvertDDSFormat(const DDS_PIXELFORMAT& ddpf, bool forceSRGB)
    {
        if (ddpf.flags & DDS_RGB)
        {
            // Note that sRGB formats are written using the "DX10" extended header

            switch (ddpf.RGBBitCount)
            {
            case 32:
                if (ISBITMASK(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
                {
                    return forceSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                }

                if (ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
                {
                    return forceSRGB ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
                }

                if (ISBITMASK(0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000))
                {
                    return forceSRGB ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM; // actually BGRX8, but there's no such format in NVRHI
                }

                // No DXGI format maps to ISBITMASK(0x000000ff,0x0000ff00,0x00ff0000,0x00000000) aka D3DFMT_X8B8G8R8

                // Note that many common DDS reader/writers (including D3DX) swap the
                // the RED/BLUE masks for 10:10:10:2 formats. We assume
                // below that the 'backwards' header mask is being used since it is most
                // likely written by D3DX. The more robust solution is to use the 'DX10'
                // header extension and specify the DXGI_FORMAT_R10G10B10A2_UNORM format directly

                // For 'correct' writers, this should be 0x000003ff,0x000ffc00,0x3ff00000 for RGB data
                if (ISBITMASK(0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000))
                {
                    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
                }

                // No DXGI format maps to ISBITMASK(0x000003ff,0x000ffc00,0x3ff00000,0xc0000000) aka D3DFMT_A2R10G10B10

                if (ISBITMASK(0x0000ffff, 0xffff0000, 0x00000000, 0x00000000))
                {
                    return VK_FORMAT_R16G16_UNORM;
                }

                if (ISBITMASK(0xffffffff, 0x00000000, 0x00000000, 0x00000000))
                {
                    // Only 32-bit color channel format in D3D9 was R32F
                    return VK_FORMAT_R32_SFLOAT;
                }
                break;

            case 24:
                // No 24bpp DXGI formats aka D3DFMT_R8G8B8
                break;

            case 16:
                if (ISBITMASK(0x7c00, 0x03e0, 0x001f, 0x8000))
                {
                    return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
                }
                if (ISBITMASK(0xf800, 0x07e0, 0x001f, 0x0000))
                {
                    return VK_FORMAT_B5G6R5_UNORM_PACK16;
                }

                // No DXGI format maps to ISBITMASK(0x7c00,0x03e0,0x001f,0x0000) aka D3DFMT_X1R5G5B5

                if (ISBITMASK(0x0f00, 0x00f0, 0x000f, 0xf000))
                {
                    return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
                }

                // No DXGI format maps to ISBITMASK(0x0f00,0x00f0,0x000f,0x0000) aka D3DFMT_X4R4G4B4

                // No 3:3:2, 3:3:2:8, or paletted DXGI formats aka D3DFMT_A8R3G3B2, D3DFMT_R3G3B2, D3DFMT_P8, D3DFMT_A8P8, etc.
                break;
            }
        }
        else if (ddpf.flags & DDS_LUMINANCE)
        {
            if (8 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x00000000))
                {
                    return VK_FORMAT_R8_UNORM;
                }

                // No DXGI format maps to ISBITMASK(0x0f,0x00,0x00,0xf0) aka D3DFMT_A4L4

                if (ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x0000ff00))
                {
                    return VK_FORMAT_R8G8_UNORM;
                }
            }

            if (16 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x0000ffff, 0x00000000, 0x00000000, 0x00000000))
                {
                    return VK_FORMAT_R16_UNORM;
                }
                if (ISBITMASK(0x000000ff, 0x00000000, 0x00000000, 0x0000ff00))
                {
                    return VK_FORMAT_R8G8_UNORM;
                }
            }
        }
        else if (ddpf.flags & DDS_ALPHA)
        {
            if (8 == ddpf.RGBBitCount)
            {
                return VK_FORMAT_R8_UNORM; // we don't support A8 in NVRHI
            }
        }
        else if (ddpf.flags & DDS_BUMPDUDV)
        {
            if (16 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x00ff, 0xff00, 0x0000, 0x0000))
                {
                    return VK_FORMAT_R8G8_SNORM;
                }
            }

            if (32 == ddpf.RGBBitCount)
            {
                if (ISBITMASK(0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
                {
                    return VK_FORMAT_R8G8B8A8_SNORM;
                }
                if (ISBITMASK(0x0000ffff, 0xffff0000, 0x00000000, 0x00000000))
                {
                    return VK_FORMAT_R16G16_SNORM;
                }

                // No DXGI format maps to ISBITMASK(0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000) aka D3DFMT_A2W10V10U10
            }
        }
        else if (ddpf.flags & DDS_FOURCC)
        {
            if (MAKEFOURCC('D', 'X', 'T', '1') == ddpf.fourCC)
            {
                return forceSRGB ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            }
            if (MAKEFOURCC('D', 'X', 'T', '3') == ddpf.fourCC)
            {
                return forceSRGB ? VK_FORMAT_BC2_SRGB_BLOCK : VK_FORMAT_BC2_UNORM_BLOCK;
            }
            if (MAKEFOURCC('D', 'X', 'T', '5') == ddpf.fourCC)
            {
                return forceSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
            }

            // While pre-multiplied alpha isn't directly supported by the DXGI formats,
            // they are basically the same as these BC formats so they can be mapped
            if (MAKEFOURCC('D', 'X', 'T', '2') == ddpf.fourCC)
            {
                return VK_FORMAT_BC2_UNORM_BLOCK;
            }
            if (MAKEFOURCC('D', 'X', 'T', '4') == ddpf.fourCC)
            {
                return VK_FORMAT_BC3_UNORM_BLOCK;
            }

            if (MAKEFOURCC('A', 'T', 'I', '1') == ddpf.fourCC)
            {
                return VK_FORMAT_BC4_UNORM_BLOCK;
            }
            if (MAKEFOURCC('B', 'C', '4', 'U') == ddpf.fourCC)
            {
                return VK_FORMAT_BC4_UNORM_BLOCK;
            }
            if (MAKEFOURCC('B', 'C', '4', 'S') == ddpf.fourCC)
            {
                return VK_FORMAT_BC4_SNORM_BLOCK;
            }

            if (MAKEFOURCC('A', 'T', 'I', '2') == ddpf.fourCC)
            {
                return VK_FORMAT_BC5_UNORM_BLOCK;
            }
            if (MAKEFOURCC('B', 'C', '5', 'U') == ddpf.fourCC)
            {
                return VK_FORMAT_BC5_UNORM_BLOCK;
            }
            if (MAKEFOURCC('B', 'C', '5', 'S') == ddpf.fourCC)
            {
                return VK_FORMAT_BC5_SNORM_BLOCK;
            }

            // BC6H and BC7 are written using the "DX10" extended header
            /*
            if (MAKEFOURCC('R', 'G', 'B', 'G') == ddpf.fourCC)
            {
                return DXGI_FORMAT_R8G8_B8G8_UNORM;
            }
            if (MAKEFOURCC('G', 'R', 'G', 'B') == ddpf.fourCC)
            {
                return DXGI_FORMAT_G8R8_G8B8_UNORM;
            }

            if (MAKEFOURCC('Y', 'U', 'Y', '2') == ddpf.fourCC)
            {
                return DXGI_FORMAT_YUY2;
            }
            */

            // Check for D3DFORMAT enums being set here
            switch (ddpf.fourCC)
            {
            case 36: // D3DFMT_A16B16G16R16
                return VK_FORMAT_R16G16B16A16_UNORM;

            case 110: // D3DFMT_Q16W16V16U16
                return VK_FORMAT_R16G16B16A16_SNORM;

            case 111: // D3DFMT_R16F
                return VK_FORMAT_R16_SFLOAT;

            case 112: // D3DFMT_G16R16F
                return VK_FORMAT_R16G16_SFLOAT;

            case 113: // D3DFMT_A16B16G16R16F
                return VK_FORMAT_R16G16B16A16_SFLOAT;

            case 114: // D3DFMT_R32F
                return VK_FORMAT_R32_SFLOAT;

            case 115: // D3DFMT_G32R32F
                return VK_FORMAT_R32G32_SFLOAT;

            case 116: // D3DFMT_A32B32G32R32F
                return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
        }

        return VK_FORMAT_UNDEFINED;
    }

    static uint32_t BitsPerPixel(VkFormat format)
    {
        const auto mapping = std::find_if(std::begin(g_FormatMappings), std::end(g_FormatMappings),
            [format](const FormatMapping& entry) { return entry.vkFormat == format; });

        return mapping != std::end(g_FormatMappings) ? mapping->bitsPerPixel : 0;
    }

    static void GetSurfaceInfo(size_t width,
        size_t height,
        VkFormat fmt,
        uint32_t bitsPerPixel,
        size_t* outNumBytes,
        size_t* outRowBytes,
        size_t* outNumRows)
    {
        size_t numBytes = 0;
        size_t rowBytes = 0;
        size_t numRows = 0;

        bool bc = false;
        size_t bpe = 0;
        switch (fmt)
        {
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            bc = true;
            bpe = 8;
            break;

        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            bc = true;
            bpe = 16;
            break;

        default:
            break;
        }

        if (bc)
        {
            size_t numBlocksWide = 0;
            if (width > 0)
            {
                numBlocksWide = std::max<size_t>(1, (width + 3) / 4);
            }
            size_t numBlocksHigh = 0;
            if (height > 0)
            {
                numBlocksHigh = std::max<size_t>(1, (height + 3) / 4);
            }
            rowBytes = numBlocksWide * bpe;
            numRows = numBlocksHigh;
            numBytes = rowBytes * numBlocksHigh;
        }
        else
        {
            rowBytes = (width * bitsPerPixel + 7) / 8; // round up to nearest byte
            numRows = height;
            numBytes = rowBytes * height;
        }

        if (outNumBytes)
        {
            *outNumBytes = numBytes;
        }
        if (outRowBytes)
        {
            *outRowBytes = rowBytes;
        }
        if (outNumRows)
        {
            *outNumRows = numRows;
        }
    }

#undef ISBITMASK

    static TextureAlphaMode GetAlphaMode(const DDS_HEADER* header)
    {
        if (header->ddspf.flags & DDS_FOURCC)
        {
            if (MAKEFOURCC('D', 'X', '1', '0') == header->ddspf.fourCC)
            {
                auto d3d10ext = reinterpret_cast<const DDS_HEADER_DXT10*>(reinterpret_cast<const uint8_t*>(header) + sizeof(DDS_HEADER));
                auto mode = static_cast<TextureAlphaMode>(d3d10ext->miscFlags2 & DDS_MISC_FLAGS2_ALPHA_MODE_MASK);
                switch (mode)
                {
                case TextureAlphaMode::STRAIGHT:
                case TextureAlphaMode::PREMULTIPLIED:
                case TextureAlphaMode::OPAQUE_:
                case TextureAlphaMode::CUSTOM:
                    return mode;

                default:
                    break;
                }
            }
            else if ((MAKEFOURCC('D', 'X', 'T', '2') == header->ddspf.fourCC)
                || (MAKEFOURCC('D', 'X', 'T', '4') == header->ddspf.fourCC))
            {
                return TextureAlphaMode::PREMULTIPLIED;
            }
        }

        return TextureAlphaMode::UNKNOWN;
    }

    static size_t FillTextureInfoOffsets(TextureData& textureInfo, size_t dataSize, ptrdiff_t dataOffset)
    {
        textureInfo.originalBitsPerPixel = BitsPerPixel(textureInfo.format);

        textureInfo.dataLayout.resize(textureInfo.arraySize);
        for (uint32_t arraySlice = 0; arraySlice < textureInfo.arraySize; arraySlice++)
        {
            size_t w = textureInfo.width;
            size_t h = textureInfo.height;
            size_t d = textureInfo.depth;

            std::vector<TextureSubresourceData>& sliceData = textureInfo.dataLayout[arraySlice];
            sliceData.resize(textureInfo.mipLevels);

            for (uint32_t mipLevel = 0; mipLevel < textureInfo.mipLevels; mipLevel++)
            {
                size_t NumBytes = 0;
                size_t RowBytes = 0;
                size_t NumRows = 0;
                GetSurfaceInfo(w, h, textureInfo.format, textureInfo.originalBitsPerPixel, &NumBytes, &RowBytes, &NumRows);

                TextureSubresourceData& levelData = sliceData[mipLevel];
                levelData.dataOffset = dataOffset;
                levelData.dataSize = NumBytes;
                levelData.rowPitch = RowBytes;
                levelData.depthPitch = RowBytes * NumRows;

                dataOffset += NumBytes * d;

                if (dataSize > 0 && dataOffset > static_cast<ptrdiff_t>(dataSize))
                    return 0;

                w = w >> 1;
                h = h >> 1;
                d = d >> 1;

                if (w == 0) w = 1;
                if (h == 0) h = 1;
                if (d == 0) d = 1;
            }
        }

        return dataOffset;
    }

    bool LoadDDSTextureFromMemory(TextureData& textureInfo)
    {
        if (textureInfo.data.size() < sizeof(uint32_t) + sizeof(DDS_HEADER))
        {
            return false;
        }

        auto dwMagicNumber = *reinterpret_cast<const uint32_t*>(textureInfo.data.data());
        if (dwMagicNumber != DDS_MAGIC)
        {
            return false;
        }

        auto header = reinterpret_cast<const DDS_HEADER*>(reinterpret_cast<const char*>(textureInfo.data.data()) + sizeof(uint32_t));

        // Verify header to validate DDS file
        if (header->size != sizeof(DDS_HEADER) ||
            header->ddspf.size != sizeof(DDS_PIXELFORMAT))
        {
            return false;
        }

        // Check for DX10 extension
        bool bDXT10Header = false;
        if ((header->ddspf.flags & DDS_FOURCC) &&
            (MAKEFOURCC('D', 'X', '1', '0') == header->ddspf.fourCC))
        {
            // Must be long enough for both headers and magic value
            if (textureInfo.data.size() < (sizeof(DDS_HEADER) + sizeof(uint32_t) + sizeof(DDS_HEADER_DXT10)))
            {
                return false;
            }

            bDXT10Header = true;
        }

        ptrdiff_t dataOffset = sizeof(uint32_t)
            + sizeof(DDS_HEADER)
            + (bDXT10Header ? sizeof(DDS_HEADER_DXT10) : 0);

        textureInfo.width = header->width;
        textureInfo.height = header->height;
        textureInfo.mipLevels = header->mipMapCount ? header->mipMapCount : 1;
        textureInfo.depth = 1;
        textureInfo.arraySize = 1;
        textureInfo.alphaMode = GetAlphaMode(header);

        if ((header->ddspf.flags & DDS_FOURCC) &&
            (MAKEFOURCC('D', 'X', '1', '0') == header->ddspf.fourCC))
        {
            auto d3d10ext = reinterpret_cast<const DDS_HEADER_DXT10*>(reinterpret_cast<const char*>(header) + sizeof(DDS_HEADER));

            // MEngine: the upper bound (D3D11's array limit) also keeps arraySize * 6 for cubes from overflowing.
            if (d3d10ext->arraySize == 0 || d3d10ext->arraySize > 2048)
            {
                return false;
            }

            for (const FormatMapping& mapping : g_FormatMappings)
            {
                if (mapping.dxgiFormat == d3d10ext->dxgiFormat)
                {
                    textureInfo.format = mapping.vkFormat;
                    break;
                }
            }

            if (textureInfo.format == VK_FORMAT_UNDEFINED)
            {
                return false;
            }

            // Apply the forceSRGB flag and promote various compatible formats to sRGB
            if (textureInfo.forceSRGB)
            {
                switch (textureInfo.format)  // NOLINT(clang-diagnostic-switch-enum)
                {
                case(VK_FORMAT_R8G8B8A8_UNORM):
                    textureInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
                    break;

                case(VK_FORMAT_B8G8R8A8_UNORM): // BGRX8_UNORM maps here too
                    textureInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
                    break;

                case(VK_FORMAT_BC1_RGBA_UNORM_BLOCK):
                    textureInfo.format = VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                    break;

                case(VK_FORMAT_BC2_UNORM_BLOCK):
                    textureInfo.format = VK_FORMAT_BC2_SRGB_BLOCK;
                    break;

                case(VK_FORMAT_BC3_UNORM_BLOCK):
                    textureInfo.format = VK_FORMAT_BC3_SRGB_BLOCK;
                    break;

                case(VK_FORMAT_BC7_UNORM_BLOCK):
                    textureInfo.format = VK_FORMAT_BC7_SRGB_BLOCK;
                    break;

                default:
                    break;
                }
            }

            switch (d3d10ext->resourceDimension)
            {
            case DDS_DIMENSION_TEXTURE1D:
                // D3DX writes 1D textures with a fixed Height of 1
                if ((header->flags & DDS_HEIGHT) && textureInfo.height != 1)
                {
                    return false;
                }
                textureInfo.height = 1;
                textureInfo.dimension = d3d10ext->arraySize > 1 ? TextureDimension::Texture1DArray : TextureDimension::Texture1D;
                break;

            case DDS_DIMENSION_TEXTURE2D:
                if (d3d10ext->miscFlag & D3D11_RESOURCE_MISC_TEXTURECUBE)
                {
                    textureInfo.arraySize = d3d10ext->arraySize * 6;
                    textureInfo.dimension = d3d10ext->arraySize > 1 ? TextureDimension::TextureCubeArray : TextureDimension::TextureCube;
                }
                else
                {
                    textureInfo.arraySize = d3d10ext->arraySize;
                    textureInfo.dimension = d3d10ext->arraySize > 1 ? TextureDimension::Texture2DArray : TextureDimension::Texture2D;
                }

                break;

            case DDS_DIMENSION_TEXTURE3D:
                if (!(header->flags & DDS_HEADER_FLAGS_VOLUME))
                {
                    return false;
                }
                textureInfo.depth = header->depth;
                textureInfo.dimension = TextureDimension::Texture3D;
                break;

            default:
                return false;
            }
        }
        else
        {
            textureInfo.format = ConvertDDSFormat(header->ddspf, textureInfo.forceSRGB);

            if (textureInfo.format == VK_FORMAT_UNDEFINED)
            {
                return false;
            }

            if (header->flags & DDS_HEADER_FLAGS_VOLUME)
            {
                textureInfo.depth = header->depth;
                textureInfo.dimension = TextureDimension::Texture3D;
            }
            else
            {
                if (header->caps2 & DDS_CUBEMAP)
                {
                    // We require all six faces to be defined
                    if ((header->caps2 & DDS_CUBEMAP_ALLFACES) != DDS_CUBEMAP_ALLFACES)
                    {
                        return false;
                    }

                    textureInfo.arraySize = 6;
                    textureInfo.dimension = TextureDimension::TextureCube;
                }
                else
                {
                    textureInfo.dimension = TextureDimension::Texture2D;
                }
            }
        }

        // MEngine: the layout below is sized from the header, so reject values a valid file cannot have (D3D11 limits)
        // before a few corrupt bytes turn into a huge allocation.
        const uint32_t largestExtent = std::max({textureInfo.width, textureInfo.height, textureInfo.depth});
        uint32_t fullMipChain = 1;
        while ((largestExtent >> fullMipChain) != 0)
        {
            ++fullMipChain;
        }
        if (largestExtent == 0 || textureInfo.mipLevels > fullMipChain || textureInfo.arraySize == 0
            || textureInfo.arraySize > 2048u * 6u)
        {
            return false;
        }

        if (FillTextureInfoOffsets(textureInfo, textureInfo.data.size(), dataOffset) == 0)
            return false;

        return true;
    }
}
