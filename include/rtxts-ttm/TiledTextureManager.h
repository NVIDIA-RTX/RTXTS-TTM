/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#pragma once

#include <stdint.h>
#include <vector>

namespace rtxts
{
    typedef uint32_t TileType;
    typedef uint8_t MipLevelType;

    static_assert(sizeof(size_t) == 8, "The hash function in TextureAndTileHash assumes 64-bit size_t");

    struct TileCoord
    {
        TileType x = 0;
        TileType y = 0;
        MipLevelType mipLevel = 0;
    };

    struct TiledLevelDesc
    {
        TileType widthInTiles;           // width of the level in tiles
        TileType heightInTiles;          // height of the level in tiles
    };

    struct TiledTextureDesc
    {
        uint32_t textureWidth;           // width of the texture in texels
        uint32_t textureHeight;          // height of the texture in texels
        TiledLevelDesc* tiledLevelDescs; // used for regular(unpacked) mip levels
        uint32_t regularMipLevelsNum;    // number of unpacked mip levels
        uint32_t packedMipLevelsNum;     // number of packed mip levels
        uint32_t packedTilesNum;         // number of tiles for packed mip levels
        uint32_t tileWidth;              // width of a tile in texels
        uint32_t tileHeight;             // height of a tile in texels
    };

    struct SamplerFeedbackDesc
    {
        uint8_t* pMinMipData = nullptr; // decoded sampler feedback data using uint8_t format, feedbackTextureWidth * feedbackTextureHeight tightly packed values should be provided
        uint32_t streamedMipLevelsNum = 0; // can be used to limit the number of mipmap levels used for data streaming
        int32_t mipLevelBias = 0;
    };

    // Abstract class for allocating and releasing heaps on the application side
    class HeapAllocator
    {
    public:
        virtual ~HeapAllocator() {};

        virtual void AllocateHeap(uint64_t heapSizeInBytes, uint32_t& heapId) = 0;
        virtual void ReleaseHeap(uint32_t heapId) = 0;
    };

    // TiledTextureManager settings which are fixed after initialization
    struct TiledTextureManagerDesc
    {
        bool alwaysMapPackedTiles = true;
        HeapAllocator* pHeapAllocator = nullptr;
        uint32_t heapTilesCapacity = 256; // number of 64KB tiles per heap, controls allocation granularity
    };

    // TiledTextureManager settings which can be changed at runtime
    struct TiledTextureManagerConfig
    {
        uint32_t maxStandbyTiles = 1000; // maximum number of tiles in the standby queue
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

    struct TextureAndTile
    {
        uint32_t textureId;
        TileType tileIndex;

        bool operator==(const TextureAndTile& other) const
        {
            return textureId == other.textureId && tileIndex == other.tileIndex;
        }

        bool operator<(const TextureAndTile& other) const
        {
            if (textureId != other.textureId)
                return textureId < other.textureId;
            return tileIndex < other.tileIndex;
        }
    };

    struct TextureAndTileHash
    {
        size_t operator()(const TextureAndTile& key) const
        {
            // Simple hash-like operation by combining both integers
            return (static_cast<size_t>(key.textureId) << 32) | key.tileIndex;
        }
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
        uint32_t totalTilesNum;      // Total number of tiles
        uint32_t allocatedTilesNum;  // Number of allocated tiles
        uint32_t standbyTilesNum;    // Number of tiles in the standby queue
    };

    class TiledTextureManager
    {
    public:
        virtual ~TiledTextureManager() {};

        // Update configuration settings which can be changed at runtime
        virtual void SetConfig(const TiledTextureManagerConfig& config) = 0;

        // Add a new texture to the manager
        virtual void AddTiledTexture(const TiledTextureDesc& tiledTextureDesc, uint32_t& textureId) = 0;

        // Remove a texture from the manager
        virtual void RemoveTiledTexture(uint32_t textureId) = 0;

        // Computes the internal state of tile streaming requests using provided sampler feedback data
        // After this, call GetTilesToMap()
        virtual void UpdateWithSamplerFeedback(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, float timeStamp, float timeout) = 0;

        // After all tiles have been updated with sampler feedback, process the standby queue and free the oldest tiles
        // Note: This currently needs to be called after UpdateWithSamplerFeedback() even if the max number of standby tiles is 0
        virtual void UpdateStandbyQueue() = 0;

        // Get a list of tiles that need to be mapped and updated.
        // Once tiles are mapped by the application, UpdateTilesMapping() should be called to update internal state
        virtual void GetTilesToMap(uint32_t textureId, std::vector<TileType>& tileIndices) = 0;

        // Updates internal state of the texture after tiles are mapped
        virtual void UpdateTilesMapping(uint32_t textureId, std::vector<TileType>& tileIndices) = 0;

        // Get a list of tiles that are no longer requested and should be unmapped from the texture
        virtual void GetTilesToUnmap(uint32_t textureId, std::vector<TileType>& tileIndices) = 0;

        // Writes MinMip residency data to a mapped texture (uint8_t per tile)
        virtual void WriteMinMipData(uint32_t textureId, uint8_t* data) = 0;

        // Finds a condidate tile to be defragmented (moved into a heap with free space)
        virtual TextureAndTile GetFragmentedTextureTile(TileAllocation& prevTileAllocation) = 0;

        // Get the description of a texture
        virtual TextureDesc GetTextureDesc(uint32_t textureId, TextureTypes textureType) const = 0;

        // Checks if a tile can currently be moved (for defragmentation)
        virtual bool IsMovableTile(uint32_t textureId, TileType tileIndex) const = 0;

        // Get the all tile coordinates for a texture
        virtual const std::vector<TileCoord>& GetTileCoordinates(uint32_t textureId) const = 0;

        // Get the current allocation state of a texture
        virtual const std::vector<TileAllocation>& GetTileAllocations(uint32_t textureId) const = 0;

        // Statistics
        virtual Statistics GetStatistics() const = 0;
    };

    TiledTextureManager* CreateTiledTextureManager(const TiledTextureManagerDesc& desc);
} // rtxts
