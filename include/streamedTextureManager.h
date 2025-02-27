/*
* Copyright (c) 2024, NVIDIA CORPORATION. All rights reserved.
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

#pragma once

#include <stdint.h>
#include <vector>

namespace rtxts
{
    typedef uint32_t TileType;
    typedef uint8_t MipLevelType;

    struct TileCoord
    {
        TileType x = 0;
        TileType y = 0;
        MipLevelType mipLevel = 0;
    };

    struct TiledLevelDesc
    {
        TileType widthInTiles;
        TileType heightInTiles;
    };

    struct TiledTextureDesc
    {
        uint32_t textureWidth;
        uint32_t textureHeight;
        TiledLevelDesc* tiledLevelDescs; // used for regular(unpacked) mip levels
        uint32_t regularMipLevelsNum;
        uint32_t packedMipLevelsNum;
        uint32_t packedTilesNum;
        uint32_t tileWidth;
        uint32_t tileHeight;
    };

    struct SamplerFeedbackDesc
    {
        uint8_t* pMinMipData = nullptr; // decoded sampler feedback data using uint8_t format, feedbackTextureWidth * feedbackTextureHeight tightly packed values should be provided
        uint32_t streamedMipLevelsNum = 0; // can be used to limit the number of mipmap levels used for data streaming
        int32_t mipLevelBias = 0;
    };

    class HeapAllocator
    {
    public:
        virtual ~HeapAllocator() {};

        virtual void AllocateHeap(uint64_t heapSize, uint32_t& heapId) = 0;
        virtual void ReleaseHeap(uint32_t heapId) = 0;
    };

    struct StreamedTextureManagerDesc
    {
        bool alwaysMapPackedTiles = true;
        HeapAllocator* pHeapAllocator = nullptr;
        uint32_t heapTilesCapacity = 256; // number of 64KB tiles per heap, controls allocation granularity
    };

    enum TextureTypes
    {
        eFeedbackTexture,
        eMinMipTexture
    };

    struct TextureDesc
    {
        uint32_t textureOrMipRegionWidth;
        uint32_t textureOrMipRegionHeight;
        uint32_t mipLevelsNum;
    };

    // Heap internal structure which points back to the texture
    struct TileAllocationInHeap
    {
        uint32_t textureId;
        uint32_t textureTileIndex;
    };

    // Tile allocation of a slot in a heap
    struct TileAllocation
    {
        uint32_t heapId = 0;
        uint32_t heapTileIndex = UINT32_MAX;
        void* pHeap = nullptr;
    };

    struct Statistics
    {
        uint32_t allocatedTilesNum;
        uint32_t totalTilesNum;
        uint64_t heapAllocatedBytes;
        uint64_t heapTotalBytes;
    };

    class StreamedTextureManager
    {
    public:
        virtual ~StreamedTextureManager() {};

        virtual void AddStreamedTexture(const TiledTextureDesc& tiledTextureDesc, uint32_t& textureId) = 0;
        virtual void RemoveStreamedTexture(uint32_t textureId) = 0;

        virtual void UpdateWithSamplerFeedback(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, uint32_t timeStamp, uint32_t timeout) = 0;

        // Get a list of tiles that need to be mapped and updated.
        // Once tiles are ready, UpdateTilesMapping() should be called to update the internal state
        virtual void GetTilesToMap(uint32_t textureId, std::vector<TileType>& tileIndices) = 0;
        virtual void UpdateTilesMapping(uint32_t textureId, std::vector<TileType>& tileIndices) = 0;

        // Get a list of tiles that are no longer requested and should be unmapped from the texture
        virtual void GetTilesToUnmap(uint32_t textureId, std::vector<TileType>& tileIndices) = 0;

        virtual void WriteMinMipData(uint32_t textureId, uint8_t* data) = 0;

        virtual TileAllocationInHeap GetFragmentedTextureTile(TileAllocation& prevTileAllocation) = 0;

        // Helper functions
        virtual TextureDesc GetTextureDesc(uint32_t textureId, TextureTypes textureType) const = 0;
        virtual bool IsMovableTile(uint32_t textureId, TileType tileIndex) const = 0;

        virtual const std::vector<TileCoord>& GetTileCoordinates(uint32_t textureId) const = 0;
        virtual const std::vector<TileAllocation>& GetTileAllocations(uint32_t textureId) const = 0;

        // Statistics
        virtual Statistics GetStatistics() const = 0;
    };

    StreamedTextureManager* CreateStreamedTextureManager(const StreamedTextureManagerDesc& desc);
} // rtxts
