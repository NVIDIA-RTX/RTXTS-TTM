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

#include <vector>
#include <iterator>
#include <list>
#include <unordered_map>

namespace rtxts
{
    class BitArray
    {
    public:
        // Iterator which returns all set bits
        struct SetBitIterator
        {
            using iterator_category = std::input_iterator_tag;
            using difference_type = std::uint32_t;
            using value_type = uint32_t;

            SetBitIterator(BitArray* pBits, uint32_t index, bool recede = false) :
                m_pBits(pBits),
                m_index(index)
            {
                if (recede)
                    while (ShouldRecede())
                        --m_index;
                else
                    while (ShouldAdvance())
                        ++m_index;
            }

            value_type operator*() const { return m_index; }

            SetBitIterator& operator++()
            {
                if (m_index < m_pBits->m_bitsNum)
                    ++m_index;

                while (ShouldAdvance())
                    ++m_index;

                return *this;
            }
            void operator++(int) { ++*this; }

            SetBitIterator& operator--()
            {
                if (m_index >= 0)
                    --m_index;

                while (ShouldRecede())
                    --m_index;

                return *this;
            }
            void operator--(int) { --*this; }

            friend bool operator== (const SetBitIterator& a, const SetBitIterator& b) { return a.m_pBits == b.m_pBits && a.m_index == b.m_index; };
            friend bool operator!= (const SetBitIterator& a, const SetBitIterator& b) { return a.m_pBits != b.m_pBits || a.m_index != b.m_index; };

        private:
            bool ShouldAdvance()
            {
                return m_index < m_pBits->m_bitsNum && !m_pBits->GetBit(m_index);
            }

            bool ShouldRecede()
            {
                return m_index >= 0 && m_index != UINT32_MAX && !m_pBits->GetBit(m_index);
            }

            BitArray* m_pBits;
            uint32_t m_index;
        };

        BitArray() :
            m_bitsNum(0),
            m_wordsNum(0),
            m_words()
        {
        }

        BitArray(const BitArray& b) :
            m_bitsNum(b.m_bitsNum),
            m_wordsNum(b.m_wordsNum),
            m_words(b.m_words)
        {
        }

        void Init(uint32_t numbits)
        {
            m_bitsNum = numbits;
            m_wordsNum = ((m_bitsNum - 1) / 64) + 1;
            m_words.resize(m_wordsNum);
        }

        void Clear()
        {
            std::fill(m_words.begin(), m_words.end(), 0);
        }

        void operator&=(const BitArray& b)
        {
            for (uint32_t i = 0; i < m_wordsNum; i++)
                m_words[i] &= b.m_words[i];
        }

        void operator|=(const BitArray& b)
        {
            for (uint32_t i = 0; i < m_wordsNum; i++)
                m_words[i] |= b.m_words[i];
        }

        void operator^=(const BitArray& b)
        {
            for (uint32_t i = 0; i < m_wordsNum; i++)
                m_words[i] ^= b.m_words[i];
        }

        bool operator==(const BitArray& b)
        {
            for (uint32_t i = 0; i < m_wordsNum; i++)
                if (m_words[i] != b.m_words[i])
                    return false;
            return true;
        }

        BitArray operator&(const BitArray& b)
        {
            BitArray a = *this;
            a &= b;
            return a;
        }

        BitArray operator|(const BitArray& b)
        {
            BitArray a = *this;
            a |= b;
            return a;
        }

        BitArray operator^(const BitArray& b)
        {
            BitArray a = *this;
            a ^= b;
            return a;
        }

        void SetBit(uint32_t index)
        {
            uint64_t mask = 1ui64 << (index & 63);
            m_words[index >> 6] |= mask;
        }

        void ClearBit(uint32_t index)
        {
            uint64_t mask = 1ui64 << (index & 63);
            m_words[index >> 6] &= ~mask;
        }

        bool GetBit(uint32_t index) const
        {
            uint64_t mask = 1ui64 << (index & 63);
            return m_words[index >> 6] & mask;
        }

        uint32_t BitCount()
        {
            uint32_t bitCount = 0;
            for (uint32_t i = 0; i < m_wordsNum; i++)
                bitCount += static_cast<uint32_t>(__popcnt64(m_words[i]));

            return bitCount;
        }

        bool IsEmpty() const
        {
            for (uint32_t i = 0; i < m_wordsNum; i++)
                if (m_words[i] != 0)
                    return false;
            return true;
        }

        SetBitIterator begin()
        {
            return SetBitIterator(this, 0);
        }

        SetBitIterator end()
        {
            return SetBitIterator(this, m_bitsNum);
        }

        SetBitIterator rbegin()
        {
            return SetBitIterator(this, m_bitsNum - 1, true);
        }

        SetBitIterator rend()
        {
            return SetBitIterator(this, -1, true);
        }

    private:
        uint32_t m_bitsNum;
        uint32_t m_wordsNum;
        std::vector<uint64_t> m_words;
    };

    // Least-Recently-Used container for caching tiles
    template <typename T, typename Hash>
    class LRUQueue {
    private:
        std::list<T> list;
        std::unordered_map<T, typename std::list<T>::iterator, Hash> map;

    public:
        void push_back(const T& val)
        {  
            list.push_back(val);
            map[val] = --list.end();
        }

        void pop_front()
        {
            if (!list.empty())
            {
                map.erase(list.front());
                list.pop_front();
            }
        }

        const T& front() const
        {
            return list.front();
        }

        bool contains(const T& val) const
        {
            return map.find(val) != map.end();
        }

        void erase(const T& val)
        {
            auto it = map.find(val);
            if (it != map.end())
            {
                list.erase(it->second);
                map.erase(it);
            }
        }

        size_t size() const
        {
            return list.size();
        }
    };

    static uint32_t PrevPowerOf2(uint32_t x)
    {
        x = x | (x >> 1);
        x = x | (x >> 2);
        x = x | (x >> 4);
        x = x | (x >> 8);
        x = x | (x >> 16);
        return x - (x >> 1);
    }

    static uint32_t RoundUp(uint32_t value, uint32_t alignment)
    {
        return ((value + (alignment - 1)) / alignment) * alignment;
    }
} // rtxts
