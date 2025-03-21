/*
 * Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "tiledTextureManagerImpl.h"

namespace rtxts
{
    TiledTextureManagerImpl::TiledTextureManagerImpl(const TiledTextureManagerDesc& tiledTextureManagerDesc)
        : m_tiledTextureManagerDesc(tiledTextureManagerDesc),
          m_totalTilesNum(0)
    {
        m_tileAllocator = std::make_shared<TileAllocator>(tiledTextureManagerDesc.heapTilesCapacity, 65536, tiledTextureManagerDesc.pHeapAllocator);
    }

    TiledTextureManagerImpl::~TiledTextureManagerImpl()
    {

    }

    void TiledTextureManagerImpl::AddTiledTexture(const TiledTextureDesc& tiledTextureDesc, uint32_t& textureId)
    {
        TextureReference texture = m_tiledTextures.Create();
        textureId = texture.Id();
        if (!texture.IsValid())
            return;

        InitTiledTexture(texture, tiledTextureDesc);

        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);
        const TiledTextureInternalDesc& desc = m_tiledTextureDescs[tiledTextureState.descriptorIndex];
        m_totalTilesNum += desc.packedTilesNum + desc.regularTilesNum;
    }

    void TiledTextureManagerImpl::RemoveTiledTexture(uint32_t textureId)
    {
        TextureReference texture({ textureId });
        if (!texture.IsValid())
            return;

        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);
        for (auto& tileAllocation : tiledTextureState.tileAllocations)
            m_tileAllocator->FreeTile(tileAllocation);

        const TiledTextureInternalDesc& desc = m_tiledTextureDescs[tiledTextureState.descriptorIndex];
        m_totalTilesNum -= desc.packedTilesNum + desc.regularTilesNum;

        tiledTextureState = {};

        m_tiledTextures.Release(texture);
    }

    void TiledTextureManagerImpl::UpdateWithSamplerFeedback(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, float timestamp, float timeout)
    {
        TextureReference texture({ textureId });
        UpdateTiledTexture(texture, samplerFeedbackDesc, timestamp, timeout);
    }

    void TiledTextureManagerImpl::GetTilesToMap(uint32_t textureId, std::vector<TileType>& tileIndices)
    {
        TextureReference texture({ textureId });
        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);

        tileIndices = tiledTextureState.tilesToMap;
        tiledTextureState.tilesToMap.clear();
    }

    void TiledTextureManagerImpl::UpdateTilesMapping(uint32_t textureId, std::vector<TileType>& tileIndices)
    {
        TextureReference texture({ textureId });
        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);

        for (auto& tileIndex : tileIndices)
            tiledTextureState.mappedBits.SetBit(tileIndex);
    }

    void TiledTextureManagerImpl::GetTilesToUnmap(uint32_t textureId, std::vector<TileType>& tileIndices)
    {
        TextureReference texture({ textureId });
        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);

        tileIndices = tiledTextureState.tilesToUnmap;
        tiledTextureState.tilesToUnmap.clear();
    }

    void TiledTextureManagerImpl::WriteMinMipData(uint32_t textureId, uint8_t* data)
    {
        TextureReference texture({ textureId });
        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);
        const TiledTextureInternalDesc& desc = m_tiledTextureDescs[tiledTextureState.descriptorIndex];

        uint32_t minMipTilesNum = desc.regularTilesNum ? desc.mipLevelTilingDescs[0].tilesX * desc.mipLevelTilingDescs[0].tilesY : 1;
        // Clear with the highest packed MIP index
        for (uint32_t i = 0; i < minMipTilesNum; ++i)
            data[i] = (uint8_t)desc.regularMipLevelsNum;

        // Now loop through allocations and update MinMip
        // Iterate backwards (lower res to higher res tiles) and only increment if the mip chain is contiguous to avoid artifacts with missing tiles in the middle
        if (desc.regularTilesNum)
        {
            for (auto it = BitArray::SetBitIterator(&tiledTextureState.mappedBits, desc.regularTilesNum - 1, true); it != tiledTextureState.mappedBits.rend(); it--)
            {
                uint32_t tileIndex = *it;
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

    TileAllocationInHeap TiledTextureManagerImpl::GetFragmentedTextureTile(TileAllocation& prevTileAllocation)
    {
        TileAllocationInHeap tileAllocation = m_tileAllocator->GetFragmentedTextureTile((TiledTextureManager*)this);
        if (tileAllocation.textureId)
        {
            TextureReference texture({ tileAllocation.textureId });
            TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);

            // Free tile from its current allocation
            TileAllocation oldAllocation = tiledTextureState.tileAllocations[tileAllocation.textureTileIndex];
            m_tileAllocator->FreeTile(oldAllocation);

            // Allocate tile again
            tiledTextureState.tileAllocations[tileAllocation.textureTileIndex] = m_tileAllocator->AllocateTile(tileAllocation.textureId, tileAllocation.textureTileIndex);
            tiledTextureState.mappedBits.ClearBit(tileAllocation.textureTileIndex);

            prevTileAllocation = oldAllocation;
        }

        return tileAllocation;
    }

    const std::vector<TileCoord>& TiledTextureManagerImpl::GetTileCoordinates(uint32_t textureId) const
    {
        TextureReference texture({ textureId });
        const TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);

        return m_tiledTextureDescs[tiledTextureState.descriptorIndex].tileIndexToTileCoord;
    }

    const std::vector<TileAllocation>& TiledTextureManagerImpl::GetTileAllocations(uint32_t textureId) const
    {
        TextureReference texture({ textureId });
        const TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);

        return tiledTextureState.tileAllocations;
    }

    TextureDesc TiledTextureManagerImpl::GetTextureDesc(uint32_t textureId, TextureTypes textureType) const
    {
        TextureReference texture({ textureId });
        const TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);
        const TiledTextureInternalDesc& desc = m_tiledTextureDescs[tiledTextureState.descriptorIndex];

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

    bool TiledTextureManagerImpl::IsMovableTile(uint32_t textureId, TileType tileIndex) const
    {
        TextureReference texture({ textureId });
        const TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);
        const TiledTextureInternalDesc& desc = m_tiledTextureDescs[tiledTextureState.descriptorIndex];

        return (tileIndex < desc.regularTilesNum) && (tiledTextureState.mappedBits.GetBit(tileIndex));
    }

    Statistics TiledTextureManagerImpl::GetStatistics() const
    {
        Statistics statistics = {};

        if (m_tileAllocator)
        {
            statistics.totalTilesNum = m_totalTilesNum;
            statistics.allocatedTilesNum = m_tileAllocator->GetAllocatedTilesNum();
        }

        return statistics;
    }

    void TiledTextureManagerImpl::InitTiledTexture(TextureReference& texture, const TiledTextureDesc& tiledTextureDesc)
    {
        TiledTextureInternalDesc desc = {};
        desc.regularTilesNum = 0;
        desc.mipLevelTilingDescs.resize(tiledTextureDesc.regularMipLevelsNum);
        for (uint32_t i = 0; i < tiledTextureDesc.regularMipLevelsNum; ++i)
        {
            desc.mipLevelTilingDescs[i].firtsTileIndex = desc.regularTilesNum;
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
        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);
        uint32_t tilesNum = desc.regularTilesNum + desc.packedTilesNum;
        tiledTextureState.lastRequestedTime.resize(tilesNum);
        tiledTextureState.tileAllocations.resize(tilesNum);
        tiledTextureState.allocatedBits.Init(tilesNum);
        tiledTextureState.mappedBits.Init(tilesNum);

        if (m_tiledTextureManagerDesc.alwaysMapPackedTiles)
            for (uint32_t i = 0; i < tiledTextureDesc.packedTilesNum; ++i)
                AllocateTile(tiledTextureState, texture.Id(), desc.regularTilesNum + i);

        uint32_t descsNum = (uint32_t)m_tiledTextureDescs.size();
        for (uint32_t i = 0; i < descsNum; ++i)
        {
            if (std::memcmp(&m_tiledTextureDescs[i], &desc, offsetof(TiledTextureInternalDesc, feedbackTilesY) + sizeof(TiledTextureInternalDesc::feedbackTilesY)) == 0)
            {
                uint32_t blockSize = tiledTextureDesc.regularMipLevelsNum * sizeof(MipLevelTilingDesc);
                if (!tiledTextureDesc.regularMipLevelsNum || std::memcmp(&m_tiledTextureDescs[i].mipLevelTilingDescs.front(), &desc.mipLevelTilingDescs.front(), blockSize) == 0)
                {
                    tiledTextureState.descriptorIndex = i;
                    return;
                }
            }
        }

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
                        desc.tileIndexToLowerMipTileIndex[tileIndex] = desc.mipLevelTilingDescs[nextMipLevel].firtsTileIndex + tileCoord.y * desc.mipLevelTilingDescs[nextMipLevel].tilesX + tileCoord.x;
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

        tiledTextureState.descriptorIndex = descsNum;
        m_tiledTextureDescs.push_back(desc);
    }

    void TiledTextureManagerImpl::UpdateTiledTexture(TextureReference& texture, SamplerFeedbackDesc& samplerFeedbackDesc, float timestamp, float timeout)
    {
        TiledTextureState& tiledTextureState = m_tiledTextures.GetData(texture);
        const TiledTextureInternalDesc& desc = m_tiledTextureDescs[tiledTextureState.descriptorIndex];

        tiledTextureState.requestedTilesNum = desc.packedTilesNum;
        if (desc.regularMipLevelsNum == 0)
            return;

        tiledTextureState.tilesToMap.clear();
        tiledTextureState.tilesToUnmap.clear();

        BitArray requestedBits;
        requestedBits.Init(desc.regularTilesNum + desc.packedTilesNum);
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
            uint32_t lastTileIndex = desc.regularMipLevelsNum > 1 ? desc.mipLevelTilingDescs[desc.regularMipLevelsNum - 1].firtsTileIndex : 0;
            for (uint32_t tileIndex = firstTileIndex; tileIndex < lastTileIndex; ++tileIndex)
                if (requestedBits.GetBit(tileIndex))
                    requestedBits.SetBit(desc.tileIndexToLowerMipTileIndex[tileIndex]);
        }

        bool requestedUnpackedTiles = firstTileIndex != UINT32_MAX;
        if (requestedUnpackedTiles || tiledTextureState.allocatedUnpackedTilesNum)
        {
            for (TileType tileIndex = 0; tileIndex < desc.regularTilesNum; ++tileIndex)
            {
                if (requestedBits.GetBit(tileIndex))
                {
                    tiledTextureState.lastRequestedTime[tileIndex] = timestamp;
                    tiledTextureState.requestedTilesNum++;
                }
                else if (tiledTextureState.allocatedBits.GetBit(tileIndex))
                {
                    // Tile allocated but not actively requested anymore
                    float timeDelta = timestamp - tiledTextureState.lastRequestedTime[tileIndex];
                    if (timeDelta >= timeout)
                    {
                        if (tiledTextureState.mappedBits.GetBit(tileIndex))
                        {
                            FreeTile(tiledTextureState, tileIndex);
                            tiledTextureState.tilesToUnmap.push_back(tileIndex);

                            tiledTextureState.allocatedUnpackedTilesNum--;
                        }
                    }
                }
            }
        }

        if (requestedUnpackedTiles)
        {
            // Mark tiles covering packed mip levels
            for (uint32_t packedTileIndex = 0; packedTileIndex < desc.packedTilesNum; ++packedTileIndex)
                requestedBits.SetBit(desc.regularTilesNum + packedTileIndex);

            BitArray newTilesBits = (requestedBits ^ tiledTextureState.allocatedBits) & requestedBits;
            for (uint32_t tileIndex : newTilesBits)
            {
                AllocateTile(tiledTextureState, texture.Id(), tileIndex);

                if (tileIndex < desc.regularTilesNum)
                    tiledTextureState.allocatedUnpackedTilesNum++;
            }
        }
    }

    uint32_t TiledTextureManagerImpl::GetTileIndex(const TiledTextureInternalDesc& tiledTextureDesc, const TileCoord& tileCood) const
    {
        if (tileCood.mipLevel >= tiledTextureDesc.regularMipLevelsNum)
            return tiledTextureDesc.regularTilesNum;

        uint32_t start = tiledTextureDesc.mipLevelTilingDescs[tileCood.mipLevel].firtsTileIndex;
        uint32_t offset = tileCood.y * tiledTextureDesc.mipLevelTilingDescs[tileCood.mipLevel].tilesX + tileCood.x;

        return start + offset;
    }

    void TiledTextureManagerImpl::AllocateTile(TiledTextureState& tiledTextureState, uint32_t textureId, TileType tileIndex)
    {
        tiledTextureState.tileAllocations[tileIndex] = m_tileAllocator->AllocateTile(textureId, tileIndex);
        tiledTextureState.allocatedBits.SetBit(tileIndex);
        tiledTextureState.tilesToMap.push_back(tileIndex);
    }

    void TiledTextureManagerImpl::FreeTile(TiledTextureState& tiledTextureState, TileType tileIndex)
    {
        m_tileAllocator->FreeTile(tiledTextureState.tileAllocations[tileIndex]);
        tiledTextureState.tileAllocations[tileIndex] = {};

        tiledTextureState.allocatedBits.ClearBit(tileIndex);
        tiledTextureState.mappedBits.ClearBit(tileIndex);
    }

    TiledTextureManager* CreateTiledTextureManager(const TiledTextureManagerDesc& desc)
    {
        return new TiledTextureManagerImpl(desc);
    }
} // rtxts
