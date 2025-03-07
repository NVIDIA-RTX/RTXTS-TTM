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

        uint32_t GetTotalTilesNum()
        {
            return GetHeapsNum() * m_heapSizeInTiles;
        }

        uint64_t GetAllocatedBytes()
        {
            return GetAllocatedTilesNum() * m_tileSizeInBytes;
        }

        uint64_t GetTotalBytes()
        {
            return GetTotalTilesNum() * m_tileSizeInBytes;
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
