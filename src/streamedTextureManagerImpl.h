/*
* Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
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

#define ENABLE_STATISTICS 1

#include "../include/streamedTextureManager.h"
#include "streamedTextureTileHelper.h"
#include "streamedTextureTileAllocator.h"

typedef uint32_t ObjectType;

namespace rtxts
{
    struct MipLevelTilingDesc
    {
        TileType firtsTileIndex = 0;
        TileType tilesX = 0;
        TileType tilesY = 0;
    };

    struct StreamedTextureDesc
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

    struct StreamedTextureState
    {
        uint32_t allocatedUnpackedTilesNum = 0;
        uint16_t descriptorIndex = UINT16_MAX;

#if ENABLE_STATISTICS
        TileType requestedTilesNum = 0; // statistics
#endif // ENABLE_STATISTICS
        std::vector<uint32_t> lastRequestedTime;

        std::vector<TileAllocation> tileAllocations;
        std::vector<TileType> tilesToMap;
        std::vector<TileType> tilesToUnmap;

        BitArray allocatedBits; // bit set for tiles which are allocated
        BitArray mappedBits; // bit set for tiles which are mapped
    };

    struct ObjectReference
    {
        ObjectReference(uint32_t id) { objectId = id; }

        bool IsValid() const { return objectId != 0; }
        uint32_t Id() const { return objectId; }
        uint32_t Index() const { return objectId - 1; }

    private:
        uint32_t objectId;
    };

    struct TextureReference : ObjectReference
    {
        static constexpr uint32_t OBJECT_NUM_MAX = 16384;
    };

    template<typename ObjectReference, typename ObjectDataType>
    class ObjectContainer
    {
    public:
        ObjectContainer()
        {
            m_freeObjectIds.resize(ObjectReference::OBJECT_NUM_MAX);
            m_objectData.resize(ObjectReference::OBJECT_NUM_MAX);

            m_freeObjectNum = ObjectReference::OBJECT_NUM_MAX;
            for (uint32_t i = 0; i < ObjectReference::OBJECT_NUM_MAX; ++i)
                m_freeObjectIds[i] = ObjectType(i + 1);
        }

        ObjectReference Create()
        {
            if (!m_freeObjectNum)
            {
                m_freeObjectNum = 1;
                m_freeObjectIds.emplace_back((ObjectType)0);
                m_objectData.emplace_back(ObjectDataType());

                m_freeObjectIds[0] = (ObjectType)m_freeObjectIds.size();
            }

            ObjectType objectId = m_freeObjectIds[--m_freeObjectNum];

            return ObjectReference{ objectId };
        }

        void Release(const ObjectReference& objectReference)
        {
            if (!objectReference.IsValid())
                return;

            m_freeObjectIds[m_freeObjectNum++] = objectReference.Id();
            m_objectData[objectReference.Index()] = {};
        }

        ObjectDataType& GetData(ObjectReference& objectReference)
        {
            return m_objectData[objectReference.Index()];
        }

        const ObjectDataType& GetData(ObjectReference& objectReference) const
        {
            return m_objectData[objectReference.Index()];
        }

    private:
        uint32_t m_freeObjectNum;
        std::vector<ObjectType> m_freeObjectIds;
        std::vector<ObjectDataType> m_objectData;
    };

    class StreamedTextureManagerImpl : public StreamedTextureManager
    {
    public:
        ~StreamedTextureManagerImpl() override;

        StreamedTextureManagerImpl(const StreamedTextureManagerDesc& desc);

        void AddStreamedTexture(const TiledTextureDesc& tiledTextureDesc, uint32_t& textureId) override;
        void RemoveStreamedTexture(uint32_t textureId) override;

        void UpdateWithSamplerFeedback(uint32_t textureId, SamplerFeedbackDesc& samplerFeedbackDesc, uint32_t timestamp, uint32_t timeout) override;

        void GetTilesToMap(uint32_t textureId, std::vector<TileType>& tileIndices) override;
        void UpdateTilesMapping(uint32_t textureId, std::vector<TileType>& tileIndices) override;

        void GetTilesToUnmap(uint32_t textureId, std::vector<TileType>& tileIndices) override;

        void WriteMinMipData(uint32_t textureId, uint8_t* data) override;

        TileAllocationInHeap GetFragmentedTextureTile(TileAllocation& prevTileAllocation) override;

        TextureDesc GetTextureDesc(uint32_t textureId, TextureTypes textureType) const override;
        bool IsMovableTile(uint32_t textureId, TileType tileIndex) const override;

        const std::vector<TileCoord>& GetTileCoordinates(uint32_t textureId) const override;
        const std::vector<TileAllocation>& GetTileAllocations(uint32_t textureId) const override;

        Statistics GetStatistics() const override;

    private:
        void InitStreamedTexture(TextureReference& texture, const TiledTextureDesc& tiledTextureDesc);
        void UpdateStreamedTexture(TextureReference& texture, SamplerFeedbackDesc& samplerFeedbackDesc, uint32_t timeStamp, uint32_t timeout);

        uint32_t GetTileIndex(const StreamedTextureDesc& streamedTextureDesc, const TileCoord& tileCoord) const;

        void AllocateTile(StreamedTextureState& streamedTextureState, uint32_t textureId, TileType tileIndex);
        void FreeTile(StreamedTextureState& streamedTextureState, TileType tileIndex);

        std::shared_ptr<TileAllocator> m_tileAllocator;
        const StreamedTextureManagerDesc m_streamedTextureManagerDesc;

        ObjectContainer<TextureReference, StreamedTextureState> m_streamedTextures;
        std::vector<StreamedTextureDesc> m_streamedTextureDescs;

        uint32_t m_totalTilesNum;
    };
} // rtxts
