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

#define ENABLE_STATISTICS 1

#include "../include/rtxts-ttm/TiledTextureManager.h"
#include "tiledTextureManagerHelper.h"
#include "tiledTextureAllocator.h"

typedef uint32_t ObjectType;

namespace rtxts
{
    struct MipLevelTilingDesc
    {
        TileType firstTileIndex = 0;
        TileType tilesX = 0;
        TileType tilesY = 0;
    };

    struct TiledTextureSharedDesc
    {
        TileType regularTilesNum = 0;
        TileType packedTilesNum = 0;
        MipLevelType regularMipLevelsNum = 0;
        MipLevelType packedMipLevelsNum = 0;
        TileType tileWidth = 0;
        TileType tileHeight = 0;
        TileType feedbackGranularityX = 1;
        TileType feedbackGranularityY = 1;
        TileType feedbackTilesX = 0;
        TileType feedbackTilesY = 0;

        std::vector<MipLevelTilingDesc> mipLevelTilingDescs;
        std::vector<TileCoord> tileIndexToTileCoord;
        std::vector<TileType> tileIndexToLowerMipTileIndex;
    };

    enum TileState
    {
        TileState_Free,
        TileState_Allocated,
        TileState_Mapped,
        TileState_Standby,
    };

    struct TiledTextureState
    {
        uint32_t allocatedUnpackedTilesNum = 0;
        uint32_t descIndex = 0;

#if ENABLE_STATISTICS
        TileType requestedTilesNum = 0; // statistics
#endif // ENABLE_STATISTICS
        std::vector<float> lastRequestedTime;

        std::vector<TileAllocation> tileAllocations;
        std::vector<TileType> tilesToMap;
        std::vector<TileType> tilesToUnmap;

        BitArray allocatedBits; // bit set for tiles which are allocated
        BitArray mappedBits; // bit set for tiles which are mapped
        BitArray standbyBits; // bit set for tiles which are in standby
    };

    class TiledTextureManagerImpl : public TiledTextureManager
    {
    public:
        ~TiledTextureManagerImpl() override;

        TiledTextureManagerImpl(const TiledTextureManagerDesc& desc);

        void SetConfig(const TiledTextureManagerConfig& config) override;

        void AddTiledTexture(const TiledTextureDesc& tiledTextureDesc, uint32_t& textureId) override;
        void RemoveTiledTexture(uint32_t textureId) override;

        void UpdateWithSamplerFeedback(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, float timestamp, float timeout) override;

        void UpdateStandbyQueue() override;

        void GetTilesToMap(uint32_t textureId, std::vector<TileType>& tileIndices) override;
        void UpdateTilesMapping(uint32_t textureId, std::vector<TileType>& tileIndices) override;
        void GetTilesToUnmap(uint32_t textureId, std::vector<TileType>& tileIndices) override;

        void WriteMinMipData(uint32_t textureId, uint8_t* data) override;

        TextureAndTile GetFragmentedTextureTile(TileAllocation& prevTileAllocation) override;

        TextureDesc GetTextureDesc(uint32_t textureId, TextureTypes textureType) const override;
        bool IsMovableTile(uint32_t textureId, TileType tileIndex) const override;

        const std::vector<TileCoord>& GetTileCoordinates(uint32_t textureId) const override;
        const std::vector<TileAllocation>& GetTileAllocations(uint32_t textureId) const override;

        Statistics GetStatistics() const override;

    private:
        void InitTiledTexture(uint32_t textureId, const TiledTextureDesc& tiledTextureDesc);
        void UpdateTiledTexture(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, float timeStamp, float timeout);

        uint32_t GetTileIndex(const TiledTextureSharedDesc& tiledTextureDesc, const TileCoord& tileCoord) const;

        void TransitionTile(uint32_t textureId, TileType tileIndex, TileState newState);
        void RemoveTileFromStandby(uint32_t textureId, TileType tileIndex);

        std::shared_ptr<TileAllocator> m_tileAllocator;
        const TiledTextureManagerDesc m_tiledTextureManagerDesc;
        TiledTextureManagerConfig m_config;

        std::vector<TiledTextureState> m_tiledTextures;
        std::vector<TiledTextureSharedDesc> m_tiledTextureSharedDescs;
        std::vector<uint32_t> m_tiledTextureFreelist;

        LRUQueue<TextureAndTile, TextureAndTileHash> m_standbyQueue;

        uint32_t m_totalTilesNum;
    };
} // rtxts
