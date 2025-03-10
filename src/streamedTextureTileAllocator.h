/*
 * Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#pragma once

#include "../include/streamedTextureManager.h"

#include <vector>
#include <set>
#include <memory>

namespace rtxts
{
    class TiledHeap
    {
    public:
        TiledHeap(uint32_t tilesNum, uint32_t heapId);

        TileAllocation AllocateTile(uint32_t textureId, uint32_t textureTileIndex);
        void FreeTile(uint32_t heapTileIndex);

        uint32_t AllocatedTilesNum() const
        {
            return m_tilesNum - FreeTilesNum();
        }

        uint32_t FreeTilesNum() const
        {
            return (uint32_t)m_freeTileIndices.size();
        }

        uint64_t TotalTilesNum() const
        {
            return m_tilesNum;
        }

        bool IsEmpty() const
        {
            return m_freeTileIndices.size() == m_tilesNum;
        }

        const std::set<uint32_t>& GetUsedTileSet() const
        {
            return m_usedList;
        }

        const std::vector<TileAllocationInHeap>& GetAllocations() const
        {
            return m_allocations;
        }

        uint32_t GetHeapId() const { return m_heapId; }

    private:
        std::vector<uint32_t> m_freeTileIndices;
        std::set<uint32_t> m_usedList;
        std::vector<TileAllocationInHeap> m_allocations;

        const uint32_t m_tilesNum;
        const uint32_t m_heapId;
    };

    class TileAllocator
    {
    public:
        TileAllocator(uint32_t heapSizeInTiles, uint32_t tileSizeInBytes, HeapAllocator* heapAllocatior);

        std::shared_ptr<TiledHeap> FindOrAllocFreeHeap();

        TileAllocation AllocateTile(uint32_t textureId, uint32_t tileIndex);
        void FreeTile(TileAllocation& tileAllocation);

        uint32_t GetHeapsNum()
        {
            return (uint32_t)m_heaps.size();
        }

        uint32_t GetAllocatedTilesNum()
        {
            return m_allocatedTilesNum;
        }

        TileAllocationInHeap GetFragmentedTextureTile(StreamedTextureManager* streamedTextureManager) const;

    private:
        std::vector<std::shared_ptr<TiledHeap>> m_heaps;
        HeapAllocator* m_pHeapAllocator = nullptr;
        const uint32_t m_heapSizeInTiles;
        const uint32_t m_tileSizeInBytes;
        uint32_t m_allocatedTilesNum = 0;
    };
} // rtxts
