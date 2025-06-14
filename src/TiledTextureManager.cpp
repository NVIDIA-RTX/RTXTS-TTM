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

#include "TiledTextureManagerImpl.h"

#if _DEBUG
#include <assert.h>
#endif

namespace rtxts
{
    TiledTextureManagerImpl::TiledTextureManagerImpl(const TiledTextureManagerDesc& tiledTextureManagerDesc)
        : m_tiledTextureManagerDesc(tiledTextureManagerDesc)
        , m_totalTilesNum(0)
        , m_activeTilesNum(0)
        , m_config()
    {
        m_tileAllocator = std::make_shared<TileAllocator>(tiledTextureManagerDesc.heapTilesCapacity, 65536);
    }

    TiledTextureManagerImpl::~TiledTextureManagerImpl()
    {

    }

    void TiledTextureManagerImpl::SetConfig(const TiledTextureManagerConfig& config)
    {
        m_config = config;
    }

    void TiledTextureManagerImpl::AddTiledTexture(const TiledTextureDesc& tiledTextureDesc, uint32_t& textureId)
    {
        if (!m_tiledTextureFreelist.empty())
        {
            textureId = m_tiledTextureFreelist.back();
            m_tiledTextureFreelist.pop_back();
        }
        else
        {
            textureId = (uint32_t)m_tiledTextures.size();
            m_tiledTextures.push_back(TiledTextureState());
        }

        InitTiledTexture(textureId, tiledTextureDesc);

        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];
        m_totalTilesNum += desc.packedTilesNum + desc.regularTilesNum;
    }

    void TiledTextureManagerImpl::RemoveTiledTexture(uint32_t textureId)
    {
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];

        // Free all allocated tiles
        for (auto& tileAllocation : tiledTextureState.tileAllocations)
        {
            m_tileAllocator->FreeTile(tileAllocation);
            m_activeTilesNum--;
        }

        // Erase tiles which may possibly be in the requested or standby queues
        for (uint32_t tileIndex = 0; tileIndex < desc.regularTilesNum; ++tileIndex)
        {
            m_requestedQueue.erase(TextureAndTile{textureId, tileIndex});
            m_standbyQueue.erase(TextureAndTile{textureId, tileIndex});
        }

        m_totalTilesNum -= desc.packedTilesNum + desc.regularTilesNum;

        tiledTextureState = {};

        m_tiledTextureFreelist.push_back(textureId);
    }

    void TiledTextureManagerImpl::UpdateWithSamplerFeedback(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, float timestamp, float timeout)
    {
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];

        tiledTextureState.requestedTilesNum = desc.packedTilesNum;
        if (desc.regularMipLevelsNum == 0)
            return;

        tiledTextureState.tilesToMap.clear();
        tiledTextureState.tilesToUnmap.clear();

        BitArray requestedBits;
        requestedBits.Init(desc.regularTilesNum + desc.packedTilesNum);
        // Mark tiles covering packed mip levels
        for (uint32_t packedTileIndex = 0; packedTileIndex < desc.packedTilesNum; ++packedTileIndex)
            requestedBits.SetBit(desc.regularTilesNum + packedTileIndex);

        // Decode sampler feedback data in MinMip format
        uint32_t firstTileIndex = UINT32_MAX;
        if (samplerFeedbackDesc.pMinMipData)
        {
            uint32_t feedbackTilesNum = desc.feedbackTilesX * desc.feedbackTilesY;
            bool useBatchProcessing = (feedbackTilesNum % 8) == 0;
            for (uint32_t feedbackTileIndex = 0; feedbackTileIndex < feedbackTilesNum;)
            {
                if (useBatchProcessing && ((feedbackTileIndex % 8) == 0))
                {
                    const uint64_t& mipLevelData = (uint64_t&)samplerFeedbackDesc.pMinMipData[feedbackTileIndex];
                    if (mipLevelData == 0xFFFFFFFFFFFFFFFFLL)
                    {
                        feedbackTileIndex += 8;
                        continue;
                    }
                }

                const uint8_t& mipLevel = samplerFeedbackDesc.pMinMipData[feedbackTileIndex];
                if (mipLevel != 0xFF)
                {
                    TileCoord tileCoord;
                    tileCoord.mipLevel = (uint32_t)std::max(mipLevel + samplerFeedbackDesc.mipLevelBias, 0);

                    tileCoord.x = ((feedbackTileIndex % desc.feedbackTilesX) / desc.feedbackGranularityX) >> tileCoord.mipLevel;
                    tileCoord.y = ((feedbackTileIndex / desc.feedbackTilesX) / desc.feedbackGranularityY) >> tileCoord.mipLevel;

                        uint32_t tileIndex = GetTileIndex(desc, tileCoord);
                        firstTileIndex = std::min(firstTileIndex, tileIndex);
                        requestedBits.SetBit(tileIndex);
                    }

                feedbackTileIndex++;
            }

            // Propagate requested tiles to lower regular mip levels
            uint32_t lastTileIndex = desc.regularMipLevelsNum > 1 ? desc.mipLevelTilingDescs[desc.regularMipLevelsNum - 1].firstTileIndex : 0;
            for (uint32_t tileIndex = firstTileIndex; tileIndex < lastTileIndex; ++tileIndex)
                if (requestedBits.GetBit(tileIndex))
                    requestedBits.SetBit(desc.tileIndexToLowerMipTileIndex[tileIndex]);
        }

        UpdateTiledTexture(textureId, requestedBits, firstTileIndex, timestamp, timeout);
    }

    void TiledTextureManagerImpl::MatchPrimaryTexture(uint32_t primaryTextureId, uint32_t followerTextureId, float timeStamp, float timeout)
    {
        TiledTextureState& primaryTextureState = m_tiledTextures[primaryTextureId];
        TiledTextureState& followerTextureState = m_tiledTextures[followerTextureId];
        const TiledTextureSharedDesc& primaryDesc = m_tiledTextureSharedDescs[primaryTextureState.descIndex];
        const TiledTextureSharedDesc& followerDesc = m_tiledTextureSharedDescs[followerTextureState.descIndex];

        BitArray requestedBits;
        requestedBits.Init(followerDesc.regularTilesNum + followerDesc.packedTilesNum);

        // Mark tiles covering packed mip levels
        for (uint32_t packedTileIndex = 0; packedTileIndex < followerDesc.packedTilesNum; ++packedTileIndex)
            requestedBits.SetBit(followerDesc.regularTilesNum + packedTileIndex);

        followerTextureState.requestedTilesNum = followerDesc.packedTilesNum;

        uint32_t firstTileIndex = UINT32_MAX;

        // Loop over all currently being requested tiles in the primary texture
        for (uint32_t primaryTileIndex : primaryTextureState.requestedBits)
        {
            // Get the tile coordinates for the primary texture
            TileCoord primaryTileCoord = primaryDesc.tileIndexToTileCoord[primaryTileIndex];
            uint32_t primaryMipLevel = primaryTileCoord.mipLevel;

            // Compute the texel region covered by this primary tile in texels
            uint32_t primaryLeft = primaryTileCoord.x * primaryDesc.tileWidth;
            uint32_t primaryTop = primaryTileCoord.y * primaryDesc.tileHeight;
            uint32_t primaryRight = primaryLeft + primaryDesc.tileWidth;
            uint32_t primaryBottom = primaryTop + primaryDesc.tileHeight;

            // Check if follower texture has the same mip level
            if (primaryMipLevel < followerDesc.regularMipLevelsNum)
            {
                // Get the tiling description for this mip level in the follower texture
                const MipLevelTilingDesc& followerMipTilingDesc = followerDesc.mipLevelTilingDescs[primaryMipLevel];

                // Loop over all tiles in this mip level of the follower texture
                uint32_t tileStart = followerMipTilingDesc.firstTileIndex;
                uint32_t tileEnd = tileStart + (followerMipTilingDesc.tilesX * followerMipTilingDesc.tilesY);
                for (uint32_t followerTileIndex = tileStart; followerTileIndex < tileEnd; ++followerTileIndex)
                {
                    TileCoord followerTileCoord = followerDesc.tileIndexToTileCoord[followerTileIndex];

                    // Compute the texel region covered by this follower tile in texels
                    uint32_t followerLeft = followerTileCoord.x * followerDesc.tileWidth;
                    uint32_t followerTop = followerTileCoord.y * followerDesc.tileHeight;
                    uint32_t followerRight = followerLeft + followerDesc.tileWidth;
                    uint32_t followerBottom = followerTop + followerDesc.tileHeight;

                    // Check if the follower tile intersects the primary tile
                    if (followerLeft < primaryRight && followerRight > primaryLeft &&
                        followerTop < primaryBottom && followerBottom > primaryTop)
                    {
                        followerTextureState.requestedTilesNum++;
                        requestedBits.SetBit(followerTileIndex);
                        firstTileIndex = std::min(firstTileIndex, followerTileIndex);
                    }
                }
            }
        }

        UpdateTiledTexture(followerTextureId, requestedBits, firstTileIndex, timeStamp, timeout);
    }

    uint32_t TiledTextureManagerImpl::GetNumDesiredHeaps()
    {
        // Sum the number of active actively requested tiles in all textures
        uint32_t numTiles = 0;
        for (auto& texture : m_tiledTextures)
        {
            numTiles += texture.requestedTilesNum;
        }

        // Add the configurable number of standby tiles
        numTiles += m_config.numExtraStandbyTiles;

        // Calculate the number of required heaps
        uint32_t tilesPerHeap = m_tiledTextureManagerDesc.heapTilesCapacity;
        uint32_t numRequiredHeaps = (numTiles + tilesPerHeap - 1) / tilesPerHeap;
        return numRequiredHeaps;
    }

    void TiledTextureManagerImpl::AddHeap(uint32_t heapId)
    {
        m_tileAllocator->AddHeap(heapId);
    }

    void TiledTextureManagerImpl::RemoveHeap(uint32_t heapId)
    {
        m_tileAllocator->RemoveHeap(heapId);
    }

    void TiledTextureManagerImpl::TrimStandbyTiles()
    {
        while (m_standbyQueue.size() > m_config.numExtraStandbyTiles)
        {
            TextureAndTile textureAndTile = m_standbyQueue.front();
            TransitionTile(textureAndTile.textureId, textureAndTile.tileIndex, TileState_Free);
        }
    }

    void TiledTextureManagerImpl::AllocateRequestedTiles()
    {
        while (m_requestedQueue.size() > 0)
        {
            auto& textureAndTile = m_requestedQueue.front();
            bool allocSuccess = TransitionTile(textureAndTile.textureId, textureAndTile.tileIndex, TileState_Allocated);
            if (!allocSuccess)
                break; // Failed to allocate tile, probably no free space
            m_requestedQueue.pop_front();
        }
    }

    void TiledTextureManagerImpl::GetTilesToMap(uint32_t textureId, std::vector<uint32_t>& tileIndices)
    {
        tileIndices.clear();
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];

        tileIndices = tiledTextureState.tilesToMap;
        tiledTextureState.tilesToMap.clear();
    }

    void TiledTextureManagerImpl::UpdateTilesMapping(uint32_t textureId, std::vector<uint32_t>& tileIndices)
    {
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];

        for (auto& tileIndex : tileIndices)
            TransitionTile(textureId, tileIndex, TileState_Mapped);
    }

    void TiledTextureManagerImpl::GetTilesToUnmap(uint32_t textureId, std::vector<uint32_t>& tileIndices)
    {
        tileIndices.clear();
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];

        tileIndices = tiledTextureState.tilesToUnmap;
        tiledTextureState.tilesToUnmap.clear();
    }

    void TiledTextureManagerImpl::WriteMinMipData(uint32_t textureId, uint8_t* data)
    {
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];

        uint32_t minMipTilesNum = desc.regularTilesNum ? desc.mipLevelTilingDescs[0].tilesX * desc.mipLevelTilingDescs[0].tilesY : 1;
        // Clear with the highest packed MIP index
        for (uint32_t i = 0; i < minMipTilesNum; ++i)
            data[i] = (uint8_t)desc.regularMipLevelsNum;

        // Now loop through allocations and update MinMip
        // Iterate backwards (lower res to higher res tiles) and only increment if the mip chain is contiguous to avoid artifacts with missing tiles in the middle
        if (desc.regularTilesNum)
        {
            for (uint32_t tileIndex = 0; tileIndex < desc.regularTilesNum; ++tileIndex)
            {
                if (tiledTextureState.tileStates[tileIndex] != TileState_Mapped && tiledTextureState.tileStates[tileIndex] != TileState_Standby)
                    continue;

                TileCoord coord = desc.tileIndexToTileCoord[tileIndex];
                uint32_t mipLevel = coord.mipLevel;
                uint32_t tileSize = 1 << mipLevel;
                uint32_t xStart = coord.x << mipLevel;
                uint32_t yStart = coord.y << mipLevel;
                // Figure out where on the full res tile map this tile goes
                for (uint32_t y = yStart; y < yStart + tileSize; y++)
                {
                    for (uint32_t x = xStart; x < xStart + tileSize; x++)
                    {
                        if (x >= desc.mipLevelTilingDescs[0].tilesX || y >= desc.mipLevelTilingDescs[0].tilesY)
                            continue;

                        uint32_t index = y * desc.mipLevelTilingDescs[0].tilesX + x;
                        if (data[index] == mipLevel + 1)
                            data[index] = (uint8_t)mipLevel;
                    }
                }
            }
        }
    }

    void TiledTextureManagerImpl::DefragmentTiles(uint32_t numTiles)
    {
        for (uint32_t i = 0; i < numTiles; i++)
        {
            TextureAndTile tileAllocation = m_tileAllocator->GetFragmentedTextureTile((TiledTextureManager*)this);
            if (!tileAllocation.textureId)
                break;

            // Free tile from its current allocation
            TransitionTile(tileAllocation.textureId, tileAllocation.tileIndex, TileState_Free);

            // Allocate tile again
            TransitionTile(tileAllocation.textureId, tileAllocation.tileIndex, TileState_Requested);
        }
    }

    void TiledTextureManagerImpl::GetEmptyHeaps(std::vector<uint32_t>& emptyHeaps)
    {
        m_tileAllocator->GetEmptyHeaps(emptyHeaps);
    }

    const std::vector<TileCoord>& TiledTextureManagerImpl::GetTileCoordinates(uint32_t textureId) const
    {
        const TiledTextureState& tiledTextureState = m_tiledTextures[textureId];

        return m_tiledTextureSharedDescs[tiledTextureState.descIndex].tileIndexToTileCoord;
    }

    const std::vector<TileAllocation>& TiledTextureManagerImpl::GetTileAllocations(uint32_t textureId) const
    {
        const TiledTextureState& tiledTextureState = m_tiledTextures[textureId];

        return tiledTextureState.tileAllocations;
    }

    TextureDesc TiledTextureManagerImpl::GetTextureDesc(uint32_t textureId, TextureTypes textureType) const
    {
        const TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];

        TextureDesc textureDesc = {};
        switch (textureType)
        {
        case eFeedbackTexture:
            textureDesc.textureOrMipRegionWidth = desc.tileWidth / desc.feedbackGranularityX;
            textureDesc.textureOrMipRegionHeight = desc.tileHeight / desc.feedbackGranularityY;
            textureDesc.mipLevelsNum = desc.regularMipLevelsNum + desc.packedMipLevelsNum;
            break;
        case eMinMipTexture:
            textureDesc.textureOrMipRegionWidth = desc.regularTilesNum ? desc.mipLevelTilingDescs[0].tilesX : 1;
            textureDesc.textureOrMipRegionHeight = desc.regularTilesNum ? desc.mipLevelTilingDescs[0].tilesY : 1;
            textureDesc.mipLevelsNum = 1;
            break;
        }

        return textureDesc;
    }

    bool TiledTextureManagerImpl::IsMovableTile(uint32_t textureId, uint32_t tileIndex) const
    {
        const TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];

        return (tileIndex < desc.regularTilesNum) && (tiledTextureState.tileStates[tileIndex] == TileState_Mapped || tiledTextureState.tileStates[tileIndex] == TileState_Standby);
    }

    Statistics TiledTextureManagerImpl::GetStatistics() const
    {
        Statistics statistics = {};

        if (m_tileAllocator)
        {
            statistics.totalTilesNum = m_totalTilesNum;
            statistics.allocatedTilesNum = m_tileAllocator->GetAllocatedTilesNum();
            statistics.heapFreeTilesNum = m_tileAllocator->GetFreeTilesNum();
            statistics.standbyTilesNum = (uint32_t)m_standbyQueue.size();
        }

        return statistics;
    }

    void TiledTextureManagerImpl::InitTiledTexture(uint32_t textureId, const TiledTextureDesc& tiledTextureDesc)
    {
        TiledTextureSharedDesc desc = {};
        desc.regularTilesNum = 0;
        desc.mipLevelTilingDescs.resize(tiledTextureDesc.regularMipLevelsNum);
        for (uint32_t i = 0; i < tiledTextureDesc.regularMipLevelsNum; ++i)
        {
            desc.mipLevelTilingDescs[i].firstTileIndex = desc.regularTilesNum;
            desc.mipLevelTilingDescs[i].tilesX = tiledTextureDesc.tiledLevelDescs[i].widthInTiles;
            desc.mipLevelTilingDescs[i].tilesY = tiledTextureDesc.tiledLevelDescs[i].heightInTiles;

            desc.regularTilesNum += tiledTextureDesc.tiledLevelDescs[i].widthInTiles * tiledTextureDesc.tiledLevelDescs[i].heightInTiles;
        }

        if (tiledTextureDesc.packedMipLevelsNum)
            desc.packedTilesNum = tiledTextureDesc.packedTilesNum;

        desc.regularMipLevelsNum = tiledTextureDesc.regularMipLevelsNum;
        desc.packedMipLevelsNum = tiledTextureDesc.packedMipLevelsNum;
        desc.tileWidth = tiledTextureDesc.tileWidth;
        desc.tileHeight = tiledTextureDesc.tileHeight;

        // Compute feedback texture tile shape
        {
            uint32_t halfTextureWidth = tiledTextureDesc.textureWidth / 2;
            uint32_t halfTextureHeight = tiledTextureDesc.textureHeight / 2;
            uint32_t feedbackTileWidth = tiledTextureDesc.tileWidth;
            uint32_t feedbackTileHeight = tiledTextureDesc.tileHeight;

            while (feedbackTileWidth > halfTextureWidth)
                feedbackTileWidth = PrevPowerOf2(feedbackTileWidth - 1);

            while (feedbackTileHeight > halfTextureHeight)
                feedbackTileHeight = PrevPowerOf2(feedbackTileHeight - 1);

            desc.feedbackGranularityX = tiledTextureDesc.tileWidth / feedbackTileWidth;
            desc.feedbackGranularityY = tiledTextureDesc.tileHeight / feedbackTileHeight;

            desc.feedbackTilesX = (tiledTextureDesc.textureWidth - 1) / feedbackTileWidth + 1;
            desc.feedbackTilesY = (tiledTextureDesc.textureHeight - 1) / feedbackTileHeight + 1;
        }

        // Init streamed texture state
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        uint32_t tilesNum = desc.regularTilesNum + desc.packedTilesNum;
        tiledTextureState.lastRequestedTime.resize(tilesNum);
        tiledTextureState.tileAllocations.resize(tilesNum);
        tiledTextureState.requestedTilesNum = desc.packedTilesNum;

        tiledTextureState.tileStates.resize(tilesNum);
        for (uint32_t i = 0; i < tilesNum; ++i)
            tiledTextureState.tileStates[i] = TileState_Free;

        // Find an already existing shared descriptor which makes this tiled texture
        // TODO: This is a linear search and can be optimized
        uint32_t sharedDescsNum = (uint32_t)m_tiledTextureSharedDescs.size();
        bool needNewSharedDesc = true;
        for (uint32_t i = 0; i < sharedDescsNum; ++i)
        {
            if (std::memcmp(&m_tiledTextureSharedDescs[i], &desc, offsetof(TiledTextureSharedDesc, feedbackTilesY) + sizeof(TiledTextureSharedDesc::feedbackTilesY)) == 0)
            {
                uint32_t blockSize = tiledTextureDesc.regularMipLevelsNum * sizeof(MipLevelTilingDesc);
                if (!tiledTextureDesc.regularMipLevelsNum || std::memcmp(&m_tiledTextureSharedDescs[i].mipLevelTilingDescs.front(), &desc.mipLevelTilingDescs.front(), blockSize) == 0)
                {
                    tiledTextureState.descIndex = i;
                    needNewSharedDesc = false;
                    break;
                }
            }
        }

        if (needNewSharedDesc)
        {
            TileCoord tileCoord;
            uint32_t tileIndex = 0;
            uint32_t nextMipLevel = 0;
            desc.tileIndexToTileCoord.resize(tilesNum);
            desc.tileIndexToLowerMipTileIndex.resize(desc.regularTilesNum);
            for (uint32_t i = 0; i < tiledTextureDesc.regularMipLevelsNum; ++i)
            {
                tileCoord.mipLevel = i;
                nextMipLevel = i + 1;
                for (uint32_t tileY = 0; tileY < tiledTextureDesc.tiledLevelDescs[i].heightInTiles; ++tileY)
                {
                    for (uint32_t tileX = 0; tileX < tiledTextureDesc.tiledLevelDescs[i].widthInTiles; ++tileX)
                    {
                        tileCoord.x = tileX;
                        tileCoord.y = tileY;
                        desc.tileIndexToTileCoord[tileIndex] = tileCoord;

                        tileCoord.x >>= 1;
                        tileCoord.y >>= 1;

                        if (nextMipLevel < tiledTextureDesc.regularMipLevelsNum)
                            desc.tileIndexToLowerMipTileIndex[tileIndex] = desc.mipLevelTilingDescs[nextMipLevel].firstTileIndex + tileCoord.y * desc.mipLevelTilingDescs[nextMipLevel].tilesX + tileCoord.x;
                        else
                            desc.tileIndexToLowerMipTileIndex[tileIndex] = desc.regularTilesNum;

                        tileIndex++;
                    }
                }
            }

            // Packed tiles
            for (uint32_t i = 0; i < tiledTextureDesc.packedTilesNum; ++i)
            {
                uint32_t packedLevelIndex = tiledTextureDesc.regularMipLevelsNum;
                desc.tileIndexToTileCoord[desc.regularTilesNum + i].x = i;
                desc.tileIndexToTileCoord[desc.regularTilesNum + i].y = 0;
                desc.tileIndexToTileCoord[desc.regularTilesNum + i].mipLevel = packedLevelIndex;
            }

            tiledTextureState.descIndex = sharedDescsNum;
            m_tiledTextureSharedDescs.push_back(desc);
        }

        // Map packed tiles
        for (uint32_t i = 0; i < tiledTextureDesc.packedTilesNum; ++i)
            TransitionTile(textureId, desc.regularTilesNum + i, TileState_Requested);
    }

    void TiledTextureManagerImpl::UpdateTiledTexture(uint32_t textureId, BitArray requestedBits, uint32_t firstTileIndex, float timestamp, float timeout)
    {
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];

        // Save requested bites for use in follower textures
        tiledTextureState.requestedBits = requestedBits;

        tiledTextureState.requestedTilesNum = desc.packedTilesNum;
        if (desc.regularMipLevelsNum == 0)
            return;

        bool requestedUnpackedTiles = firstTileIndex != UINT32_MAX;
        if (requestedUnpackedTiles || tiledTextureState.allocatedUnpackedTilesNum)
        {
            for (uint32_t tileIndex = 0; tileIndex < desc.regularTilesNum; ++tileIndex)
            {
                if (requestedBits.GetBit(tileIndex))
                {
                    // Tile is being requested
                    tiledTextureState.lastRequestedTime[tileIndex] = timestamp;
                    tiledTextureState.requestedTilesNum++;

                    if (tiledTextureState.tileStates[tileIndex] == TileState_Standby)
                    {
                        // Tile is in standby queue, transition it back to mapped state and remove from standby queue
                        TransitionTile(textureId, tileIndex, TileState_Mapped);
                    }
                    else if (tiledTextureState.tileStates[tileIndex] == TileState_Free)
                    {
                        // Tile is free, transition it to requested state
                        TransitionTile(textureId, tileIndex, TileState_Requested);
                    }
                }
                else if (tiledTextureState.tileStates[tileIndex] == TileState_Mapped)
                {
                    // Tile allocated but not actively requested anymore
                    float timeDelta = timestamp - tiledTextureState.lastRequestedTime[tileIndex];
                    if (timeDelta >= timeout)
                    {
                        // Timeout condition met
                        if (tiledTextureState.tileStates[tileIndex] == TileState_Mapped)
                        {
                            // Put the tile in standby queue
                            TransitionTile(textureId, tileIndex, TileState_Standby);
                        }
                    }
                }
            }
        }
    }

    uint32_t TiledTextureManagerImpl::GetTileIndex(const TiledTextureSharedDesc& tiledTextureDesc, const TileCoord& tileCood) const
    {
        if (tileCood.mipLevel >= tiledTextureDesc.regularMipLevelsNum)
            return tiledTextureDesc.regularTilesNum;

        uint32_t start = tiledTextureDesc.mipLevelTilingDescs[tileCood.mipLevel].firstTileIndex;
        uint32_t offset = tileCood.y * tiledTextureDesc.mipLevelTilingDescs[tileCood.mipLevel].tilesX + tileCood.x;

        return start + offset;
    }

    bool TiledTextureManagerImpl::TransitionTile(uint32_t textureId, uint32_t tileIndex, TileState newState)
    {
        TiledTextureState& tiledTextureState = m_tiledTextures[textureId];
        const TiledTextureSharedDesc& desc = m_tiledTextureSharedDescs[tiledTextureState.descIndex];

        auto& tileState = tiledTextureState.tileStates[tileIndex];

#if _DEBUG
        // Cannot change to the same state
        assert(newState != tileState);
        // Assert valid state transtition logic
        switch(tileState)
        {
            case TileState_Free:
                assert(newState == TileState_Requested || newState == TileState_Standby);
                break;
            case TileState_Requested:
                assert(newState == TileState_Allocated || newState == TileState_Standby);
                break;
            case TileState_Allocated:
                assert(newState == TileState_Mapped || newState == TileState_Standby);
                break;
            case TileState_Mapped:
                assert(newState == TileState_Free || newState == TileState_Standby);
                break;
            case TileState_Standby:
                assert(newState == TileState_Free || newState == TileState_Mapped);
                break;
        }
        // Several target state checks
        switch(newState)
        {
            case TileState_Requested:
                // Tile cannot be in the requested queue already
                assert(!m_requestedQueue.contains(TextureAndTile{textureId, tileIndex}));
                break;
            case TileState_Standby:
                // Tile cannot be in the standby queue already
                assert(!m_standbyQueue.contains(TextureAndTile{textureId, tileIndex}));
                break;
        }
#endif

        // Remove from standby queue if it was previously in there
        if (tileState == TileState_Standby)
        {
            // Tile is in standby queue, remove from standby queue
            m_standbyQueue.erase(TextureAndTile{textureId, tileIndex});
        }
#if _DEBUG
        assert(!m_standbyQueue.contains(TextureAndTile{textureId, tileIndex}));
#endif

        // Perform state transition actions
        switch(newState)
        {
            case TileState_Free:
            {
                m_tileAllocator->FreeTile(tiledTextureState.tileAllocations[tileIndex]);
                tiledTextureState.tileAllocations[tileIndex] = {};
                m_activeTilesNum--;
                tiledTextureState.tilesToUnmap.push_back(tileIndex);
                if (tileIndex < desc.regularTilesNum)
                    tiledTextureState.allocatedUnpackedTilesNum--;
                break;
            }

            case TileState_Requested:
            {
                // Tile is being requested, add to requested queue
                m_requestedQueue.push_back(TextureAndTile{textureId, tileIndex});
                m_activeTilesNum++;
                break;
            }

            case TileState_Allocated:
            {
                if (m_tileAllocator->GetFreeTilesNum() == 0 && m_standbyQueue.size() > 0)
                {
                    // Remove the oldest tile from the standby queue
                    TextureAndTile textureAndTile = m_standbyQueue.front();
                    TransitionTile(textureAndTile.textureId, textureAndTile.tileIndex, TileState_Free);
                }
                TileAllocation alloc = m_tileAllocator->AllocateTile(textureId, tileIndex);
                if (!alloc.IsValid())
                {
                    // Failed to allocate this tile
                    return false;
                }
                tiledTextureState.tileAllocations[tileIndex] = alloc;
                tiledTextureState.tilesToMap.push_back(tileIndex);
                if (tileIndex < desc.regularTilesNum)
                    tiledTextureState.allocatedUnpackedTilesNum++;
                break;
            }
            case TileState_Mapped:
            {
                break;
            }
            case TileState_Standby:
            {
                m_standbyQueue.push_back(TextureAndTile{textureId, tileIndex});
                break;
            }
        }

        tileState = newState;
        return true;
    }

    TiledTextureManager* CreateTiledTextureManager(const TiledTextureManagerDesc& desc)
    {
        return new TiledTextureManagerImpl(desc);
    }
} // rtxts
