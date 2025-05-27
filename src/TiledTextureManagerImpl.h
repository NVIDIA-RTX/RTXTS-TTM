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

#include "../include/rtxts-ttm/TiledTextureManager.h"
#include "tiledTextureManagerHelper.h"
#include "tiledTextureAllocator.h"

typedef uint32_t ObjectType;

namespace rtxts
{
    struct MipLevelTilingDesc
    {
        uint32_t firstTileIndex = 0;
        uint32_t tilesX = 0;
        uint32_t tilesY = 0;
    };

    struct TiledTextureSharedDesc
    {
        uint32_t regularTilesNum = 0;
        uint32_t packedTilesNum = 0;
        uint8_t regularMipLevelsNum = 0;
        uint8_t packedMipLevelsNum = 0;
        uint32_t tileWidth = 0;
        uint32_t tileHeight = 0;
        uint32_t feedbackGranularityX = 1;
        uint32_t feedbackGranularityY = 1;
        uint32_t feedbackTilesX = 0;
        uint32_t feedbackTilesY = 0;

        std::vector<MipLevelTilingDesc> mipLevelTilingDescs;
        std::vector<TileCoord> tileIndexToTileCoord;
        std::vector<uint32_t> tileIndexToLowerMipTileIndex;
    };

    // Tile state which implements a state machine for the tile
    // Valid state transitions:
    // Free -> Requested
    // Requested -> Allocated
    // Allocated -> Mapped
    // Mapped -> Free
    // Mapped -> Standby
    // Standby -> Free
    // Standby -> Mapped
    enum TileState
    {
        TileState_Free,
        TileState_Requested,
        TileState_Allocated,
        TileState_Mapped,
        TileState_Standby,
    };

    struct TiledTextureState
    {
        uint32_t allocatedUnpackedTilesNum = 0;
        uint32_t descIndex = 0;

        std::vector<float> lastRequestedTime;

        std::vector<TileAllocation> tileAllocations;
        std::vector<uint32_t> tilesToMap;
        std::vector<uint32_t> tilesToUnmap;

        std::vector<TileState> tileStates;

        uint32_t requestedTilesNum = 0; // number of tiles currently being requested by sampler feedback
        BitArray requestedBits; // tiles which are currently being actively requested (for MatchPrimaryTexture)
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
        void MatchPrimaryTexture(uint32_t primaryTextureId, uint32_t followerTextureId, float timeStamp, float timeout) override;

        uint32_t GetNumDesiredHeaps() override;

        void AddHeap(uint32_t heapId) override;
        void RemoveHeap(uint32_t heapId) override;

        void TrimStandbyTiles() override;

        void AllocateRequestedTiles() override;

        void GetTilesToMap(uint32_t textureId, std::vector<uint32_t>& tileIndices) override;
        void UpdateTilesMapping(uint32_t textureId, std::vector<uint32_t>& tileIndices) override;
        void GetTilesToUnmap(uint32_t textureId, std::vector<uint32_t>& tileIndices) override;

        void WriteMinMipData(uint32_t textureId, uint8_t* data) override;

        void DefragmentTiles(uint32_t numTiles) override;

        void GetEmptyHeaps(std::vector<uint32_t>& emptyHeaps) override;

        TextureDesc GetTextureDesc(uint32_t textureId, TextureTypes textureType) const override;
        bool IsMovableTile(uint32_t textureId, uint32_t tileIndex) const override;

        const std::vector<TileCoord>& GetTileCoordinates(uint32_t textureId) const override;
        const std::vector<TileAllocation>& GetTileAllocations(uint32_t textureId) const override;

        Statistics GetStatistics() const override;

    private:
        void InitTiledTexture(uint32_t textureId, const TiledTextureDesc& tiledTextureDesc);
        void UpdateTiledTexture(uint32_t textureId, BitArray requestedBits, uint32_t firstTileIndex, float timeStamp, float timeout);

        uint32_t GetTileIndex(const TiledTextureSharedDesc& tiledTextureDesc, const TileCoord& tileCoord) const;

        bool TransitionTile(uint32_t textureId, uint32_t tileIndex, TileState newState);

        std::shared_ptr<TileAllocator> m_tileAllocator;
        const TiledTextureManagerDesc m_tiledTextureManagerDesc;
        TiledTextureManagerConfig m_config;

        std::vector<TiledTextureState> m_tiledTextures;
        std::vector<TiledTextureSharedDesc> m_tiledTextureSharedDescs;
        std::vector<uint32_t> m_tiledTextureFreelist;

        LRUQueue<TextureAndTile, TextureAndTileHash> m_requestedQueue; // Tiles which are waiting to be allocated
        LRUQueue<TextureAndTile, TextureAndTileHash> m_standbyQueue; // Tiles which are currently in standby

        uint32_t m_totalTilesNum; // Total number of tiles in all textures
        uint32_t m_activeTilesNum; // Total number of active (requested+allocated) tiles in all textures
    };
} // rtxts
