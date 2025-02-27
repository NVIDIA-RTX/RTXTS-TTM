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

#include "streamedTextureTileAllocator.h"

namespace rtxts
{
    TiledHeap::TiledHeap(uint32_t tilesNum, uint32_t heapId)
        : m_tilesNum(tilesNum)
        , m_heapId(heapId)
    {
        m_freeTileIndices.resize(m_tilesNum);
        m_allocations.resize(m_tilesNum);
        for (uint32_t i = 0; i < m_tilesNum; ++i)
        {
            m_freeTileIndices[i] = i;
            m_allocations[i] = {};
        }
    }

    TileAllocation TiledHeap::AllocateTile(uint32_t textureId, uint32_t textureTileIndex)
    {
        uint32_t heapTileIndex = m_freeTileIndices.back();
        m_freeTileIndices.pop_back();
        m_usedList.insert(heapTileIndex);

        auto& textureAllocation = m_allocations[heapTileIndex];
        textureAllocation.textureId = textureId;
        textureAllocation.textureTileIndex = textureTileIndex;

        TileAllocation heapAllocation;
        heapAllocation.heapId = m_heapId;
        heapAllocation.heapTileIndex = heapTileIndex;
        heapAllocation.pHeap = this;

        return heapAllocation;
    }

    void TiledHeap::FreeTile(uint32_t heapTileIndex)
    {
        m_usedList.erase(heapTileIndex);
        m_freeTileIndices.push_back(heapTileIndex);
        m_allocations[heapTileIndex] = {};
    }

    TileAllocator::TileAllocator(uint32_t heapSizeInTiles, uint32_t tileSizeInBytes, HeapAllocator* pHeapAllocator)
        : m_heapSizeInTiles(heapSizeInTiles)
        , m_tileSizeInBytes(tileSizeInBytes)
        , m_pHeapAllocator(pHeapAllocator)
        , m_allocatedTilesNum(0)
    {
    }

    std::shared_ptr<TiledHeap> TileAllocator::FindOrAllocFreeHeap()
    {
        for (auto heap : m_heaps)
            if (heap->FreeTilesNum())
                return heap;

        uint32_t heapId;
        m_pHeapAllocator->AllocateHeap(m_heapSizeInTiles * m_tileSizeInBytes, heapId);

        m_heaps.push_back(std::make_shared<TiledHeap>(m_heapSizeInTiles, heapId));

        return m_heaps.back();
    }

    TileAllocation TileAllocator::AllocateTile(uint32_t textureId, uint32_t tileIndex)
    {
        TileAllocation tileAllocation = {};

        auto pHeap = FindOrAllocFreeHeap();
        if (!pHeap)
            return tileAllocation;

        m_allocatedTilesNum++;

        return pHeap->AllocateTile(textureId, tileIndex);
    }

    void TileAllocator::FreeTile(TileAllocation& tileAllocation)
    {
        if (!tileAllocation.pHeap)
            return;

        TiledHeap* pTiledHeap = reinterpret_cast<TiledHeap*>(tileAllocation.pHeap);
        pTiledHeap->FreeTile(tileAllocation.heapTileIndex);
        m_allocatedTilesNum--;

        if (pTiledHeap->IsEmpty())
        {
            uint32_t heapId = pTiledHeap->GetHeapId();
            m_pHeapAllocator->ReleaseHeap(heapId);

            for (auto it = m_heaps.begin(); it != m_heaps.end(); ++it)
            {
                auto& heap = *it;
                if (heap->GetHeapId() == heapId)
                {
                    m_heaps.erase(it);
                    return;
                }
            }
        }
    }

    TileAllocationInHeap TileAllocator::GetFragmentedTextureTile(StreamedTextureManager* streamedTextureManager) const
    {
        TileAllocationInHeap tileAllocation = {};

        // We need at least 2 heaps
        if (m_heaps.size() < 2)
            return tileAllocation;

        // Discover if we are at all fragmented by looking at all heaps except the last
        bool isFragmented = false;
        for (uint32_t iHeap = 0; iHeap < m_heaps.size() - 1; iHeap++)
        {
            if (m_heaps[iHeap]->FreeTilesNum() > 0)
            {
                isFragmented = true;
                break;
            }
        }

        if (!isFragmented)
            return tileAllocation;

        // Iterate from the back to find a tile to defragment
        for (uint32_t iHeap = uint32_t(m_heaps.size()) - 1; iHeap > 0; iHeap--)
        {
            auto& heap = m_heaps[iHeap];
            if (!heap->IsEmpty())
            {
                for (auto& heapAllocationIndex : heap->GetUsedTileSet())
                {
                    auto tileAllocation = heap->GetAllocations()[heapAllocationIndex];
                    if (streamedTextureManager->IsMovableTile(tileAllocation.textureId, tileAllocation.textureTileIndex))
                    {
                        return tileAllocation;
                    }
                }
            }
        }

        return tileAllocation;
    }
} // rtxts
