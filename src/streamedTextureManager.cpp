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

#include "streamedTextureManagerImpl.h"

namespace nvstm
{
    StreamedTextureManagerImpl::StreamedTextureManagerImpl(const StreamedTextureManagerDesc& streamedTextureManagerDesc)
        : m_streamedTextureManagerDesc(streamedTextureManagerDesc)
    {
        m_tileAllocator = std::make_shared<TileAllocator>(streamedTextureManagerDesc.heapTilesCapacity, 65536, streamedTextureManagerDesc.pHeapAllocator);
    }

    StreamedTextureManagerImpl::~StreamedTextureManagerImpl()
    {

    }

    void StreamedTextureManagerImpl::AddStreamedTexture(const TiledTextureDesc& tiledTextureDesc, uint32_t& textureId)
    {
        TextureReference texture = m_streamedTextures.Create();
        textureId = texture.Id();
        if (!texture.IsValid())
            return;

        InitStreamedTexture(texture, tiledTextureDesc);
    }

    void StreamedTextureManagerImpl::RemoveStreamedTexture(uint32_t textureId)
    {
        TextureReference texture({ textureId });
        if (!texture.IsValid())
            return;

        StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);
        for (auto& tileAllocation : streamedTextureState.tileAllocations)
            m_tileAllocator->FreeTile(tileAllocation);

        streamedTextureState = {};

        m_streamedTextures.Release(texture);
    }

    void StreamedTextureManagerImpl::UpdateWithSamplerFeedback(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, uint32_t timestamp, uint32_t timeout)
    {
        TextureReference texture({ textureId });
        UpdateStreamedTexture(texture, samplerFeedbackDesc, timestamp, timeout);
    }

    void StreamedTextureManagerImpl::GetTilesToMap(uint32_t textureId, std::vector<TileType>& tileIndices)
    {
        TextureReference texture({ textureId });
        StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);

        tileIndices = streamedTextureState.tilesToMap;
        streamedTextureState.tilesToMap.clear();
    }

    void StreamedTextureManagerImpl::UpdateTilesMapping(uint32_t textureId, std::vector<TileType>& tileIndices)
    {
        TextureReference texture({ textureId });
        StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);

        for (auto& tileIndex : tileIndices)
            streamedTextureState.mappedBits.SetBit(tileIndex);
    }

    void StreamedTextureManagerImpl::GetTilesToUnmap(uint32_t textureId, std::vector<TileType>& tileIndices)
    {
        TextureReference texture({ textureId });
        StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);

        tileIndices = streamedTextureState.tilesToUnmap;
        streamedTextureState.tilesToUnmap.clear();
    }

    void StreamedTextureManagerImpl::WriteMinMipData(uint32_t textureId, uint8_t* data)
    {
        TextureReference texture({ textureId });
        StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);
        const StreamedTextureDesc& desc = m_streamedTextureDescs[streamedTextureState.descriptorIndex];

        uint32_t minMipTilesNum = desc.regularTilesNum ? desc.mipLevelTilingDescs[0].tilesX * desc.mipLevelTilingDescs[0].tilesY : 1;
        // Clear with the highest packed MIP index
        for (uint32_t i = 0; i < minMipTilesNum; ++i)
            data[i] = (uint8_t)desc.regularMipLevelsNum;

        // Now loop through allocations and update MinMip
        // Iterate backwards (lower res to higher res tiles) and only increment if the mip chain is contiguous to avoid artifacts with missing tiles in the middle
        if (desc.regularTilesNum)
        {
            for (auto it = BitArray::SetBitIterator(&streamedTextureState.mappedBits, desc.regularTilesNum - 1, true); it != streamedTextureState.mappedBits.rend(); it--)
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

                        data[y * desc.mipLevelTilingDescs[0].tilesX + x] = (uint8_t)mipLevel;
                    }
                }
            }
        }
    }

    TileAllocation StreamedTextureManagerImpl::GetFragmentedTextureTile(TileAllocation& prevTileAllocation)
    {
        TileAllocation tileAllocation = m_tileAllocator->GetFragmentedTextureTile((StreamedTextureManager*)this);
        if (tileAllocation.textureId)
        {
            TextureReference texture({ tileAllocation.textureId });
            StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);

            TileAllocation oldAllocation = streamedTextureState.tileAllocations[tileAllocation.textureTileIndex];
            m_tileAllocator->FreeTile(oldAllocation);
            streamedTextureState.tileAllocations[tileAllocation.textureTileIndex] = m_tileAllocator->AllocateTile(tileAllocation.textureId, tileAllocation.textureTileIndex);
            streamedTextureState.mappedBits.ClearBit(tileAllocation.textureTileIndex);

            prevTileAllocation = oldAllocation;
        }

        return tileAllocation;
    }

    const std::vector<TileCoord>& StreamedTextureManagerImpl::GetTilesCoordinates(uint32_t textureId) const
    {
        TextureReference texture({ textureId });
        const StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);

        return m_streamedTextureDescs[streamedTextureState.descriptorIndex].tileIndexToTileCoord;
    }

    const std::vector<TileAllocation>& StreamedTextureManagerImpl::GetTilesAllocations(uint32_t textureId) const
    {
        TextureReference texture({ textureId });
        const StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);

        return streamedTextureState.tileAllocations;
    }

    TextureDesc StreamedTextureManagerImpl::GetTextureDesc(uint32_t textureId, TextureTypes textureType) const
    {
        TextureReference texture({ textureId });
        const StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);
        const StreamedTextureDesc& desc = m_streamedTextureDescs[streamedTextureState.descriptorIndex];

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

    bool StreamedTextureManagerImpl::IsMovableTile(uint32_t textureId, TileType tileIndex) const
    {
        TextureReference texture({ textureId });
        const StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);
        const StreamedTextureDesc& desc = m_streamedTextureDescs[streamedTextureState.descriptorIndex];

        return (tileIndex < desc.regularTilesNum) && (streamedTextureState.mappedBits.GetBit(tileIndex));
    }

    Statistics StreamedTextureManagerImpl::GetStatistics() const
    {
        Statistics statistics = {};

        if (m_tileAllocator)
        {
            statistics.allocatedTilesNum = m_tileAllocator->GetAllocatedTilesNum();
            statistics.totalTilesNum = m_tileAllocator->GetTotalTilesNum();
            statistics.heapAllocatedBytes = m_tileAllocator->GetAllocatedBytes();
            statistics.heapTotalBytes = m_tileAllocator->GetTotalBytes();
        }

        return statistics;
    }

    void StreamedTextureManagerImpl::InitStreamedTexture(TextureReference& texture, const TiledTextureDesc& tiledTextureDesc)
    {
        StreamedTextureDesc desc = {};
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
        StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);
        uint32_t tilesNum = desc.regularTilesNum + desc.packedTilesNum;
        streamedTextureState.lastRequestedTime.resize(tilesNum);
        streamedTextureState.tileAllocations.resize(tilesNum);
        streamedTextureState.allocatedBits.Init(tilesNum);
        streamedTextureState.mappedBits.Init(tilesNum);

        if (m_streamedTextureManagerDesc.alwaysMapPackedTiles)
            for (uint32_t i = 0; i < tiledTextureDesc.packedTilesNum; ++i)
                AllocateTile(streamedTextureState, texture.Id(), desc.regularTilesNum + i);

        uint32_t descsNum = (uint32_t)m_streamedTextureDescs.size();
        for (uint32_t i = 0; i < descsNum; ++i)
        {
            if (std::memcmp(&m_streamedTextureDescs[i], &desc, offsetof(StreamedTextureDesc, feedbackTilesY) + sizeof(StreamedTextureDesc::feedbackTilesY)) == 0)
            {
                uint32_t blockSize = tiledTextureDesc.regularMipLevelsNum * sizeof(MipLevelTilingDesc);
                if (!tiledTextureDesc.regularMipLevelsNum || std::memcmp(&m_streamedTextureDescs[i].mipLevelTilingDescs.front(), &desc.mipLevelTilingDescs.front(), blockSize) == 0)
                {
                    streamedTextureState.descriptorIndex = i;
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

        streamedTextureState.descriptorIndex = descsNum;
        m_streamedTextureDescs.push_back(desc);
    }

    void StreamedTextureManagerImpl::UpdateStreamedTexture(TextureReference& texture, SamplerFeedbackDesc& samplerFeedbackDesc, uint32_t timestamp, uint32_t timeout)
    {
        StreamedTextureState& streamedTextureState = m_streamedTextures.GetData(texture);
        const StreamedTextureDesc& desc = m_streamedTextureDescs[streamedTextureState.descriptorIndex];

        streamedTextureState.requestedTilesNum = desc.packedTilesNum;
        if (desc.regularMipLevelsNum == 0)
            return;

        streamedTextureState.tilesToMap.clear();
        streamedTextureState.tilesToUnmap.clear();

        BitArray requestedBits;
        requestedBits.Init(desc.regularTilesNum + desc.packedTilesNum);
        uint32_t firstTileIndex = UINT32_MAX;
        if (samplerFeedbackDesc.pMinMipData)
        {
            for (uint32_t feedbackY = 0; feedbackY < desc.feedbackTilesY; ++feedbackY)
            {
                for (uint32_t feedbackX = 0; feedbackX < desc.feedbackTilesX; ++feedbackX)
                {
                    TileCoord tileCoord;
                    tileCoord.mipLevel = samplerFeedbackDesc.pMinMipData[feedbackY * desc.feedbackTilesX + feedbackX];
                    if (tileCoord.mipLevel != 0xFF)
                    {
                        tileCoord.mipLevel = (uint32_t)std::max(tileCoord.mipLevel + samplerFeedbackDesc.mipLevelBias, 0);

                        tileCoord.x = (feedbackX / desc.feedbackGranularityX) >> tileCoord.mipLevel;
                        tileCoord.y = (feedbackY / desc.feedbackGranularityY) >> tileCoord.mipLevel;

                        uint32_t tileIndex = GetTileIndex(desc, tileCoord);
                        firstTileIndex = std::min(firstTileIndex, tileIndex);
                        requestedBits.SetBit(tileIndex);
                    }
                }
            }

            // Propagate requested tiles to lower regular mip levels
            uint32_t lastTileIndex = desc.regularMipLevelsNum > 1 ? desc.mipLevelTilingDescs[desc.regularMipLevelsNum - 1].firtsTileIndex : 0;
            for (uint32_t tileIndex = firstTileIndex; tileIndex < lastTileIndex; ++tileIndex)
                if (requestedBits.GetBit(tileIndex))
                    requestedBits.SetBit(desc.tileIndexToLowerMipTileIndex[tileIndex]);
        }

        bool requestedUnpackedTiles = firstTileIndex != UINT32_MAX;
        if (requestedUnpackedTiles || streamedTextureState.allocatedUnpackedTilesNum)
        {
            for (TileType tileIndex = 0; tileIndex < desc.regularTilesNum; ++tileIndex)
            {
                if (requestedBits.GetBit(tileIndex))
                {
                    streamedTextureState.lastRequestedTime[tileIndex] = timestamp;
                    streamedTextureState.requestedTilesNum++;
                }
                else if (streamedTextureState.allocatedBits.GetBit(tileIndex))
                {
                    // Tile allocated but not actively requested anymore
                    uint32_t timeDelta = timestamp - streamedTextureState.lastRequestedTime[tileIndex];
                    if (timeDelta >= timeout)
                    {
                        if (streamedTextureState.mappedBits.GetBit(tileIndex))
                        {
                            FreeTile(streamedTextureState, tileIndex);
                            streamedTextureState.tilesToUnmap.push_back(tileIndex);

                            streamedTextureState.allocatedUnpackedTilesNum--;
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

            BitArray newTilesBits = (requestedBits ^ streamedTextureState.allocatedBits) & requestedBits;
            for (uint32_t tileIndex : newTilesBits)
            {
                AllocateTile(streamedTextureState, texture.Id(), tileIndex);

                if (tileIndex < desc.regularTilesNum)
                    streamedTextureState.allocatedUnpackedTilesNum++;
            }
        }
    }

    uint32_t StreamedTextureManagerImpl::GetTileIndex(const StreamedTextureDesc& streamedTextureDesc, const TileCoord& tileCood) const
    {
        if (tileCood.mipLevel >= streamedTextureDesc.regularMipLevelsNum)
            return streamedTextureDesc.regularTilesNum;

        uint32_t start = streamedTextureDesc.mipLevelTilingDescs[tileCood.mipLevel].firtsTileIndex;
        uint32_t offset = tileCood.y * streamedTextureDesc.mipLevelTilingDescs[tileCood.mipLevel].tilesX + tileCood.x;

        return start + offset;
    }

    void StreamedTextureManagerImpl::AllocateTile(StreamedTextureState& streamedTextureState, uint32_t textureId, TileType tileIndex)
    {
        streamedTextureState.tileAllocations[tileIndex] = m_tileAllocator->AllocateTile(textureId, tileIndex);
        streamedTextureState.allocatedBits.SetBit(tileIndex);
        streamedTextureState.tilesToMap.push_back(tileIndex);
    }

    void StreamedTextureManagerImpl::FreeTile(StreamedTextureState& streamedTextureState, TileType tileIndex)
    {
        m_tileAllocator->FreeTile(streamedTextureState.tileAllocations[tileIndex]);
        streamedTextureState.tileAllocations[tileIndex] = {};

        streamedTextureState.allocatedBits.ClearBit(tileIndex);
        streamedTextureState.mappedBits.ClearBit(tileIndex);
    }

    StreamedTextureManager* CreateStreamedTextureManager(const StreamedTextureManagerDesc& desc)
    {
        return new StreamedTextureManagerImpl(desc);
    }
} // nvstm
