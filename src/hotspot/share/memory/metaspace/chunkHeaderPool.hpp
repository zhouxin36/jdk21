/*
 * Copyright (c) 2020, 2023, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2020 SAP SE. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_MEMORY_METASPACE_CHUNKHEADERPOOL_HPP
#define SHARE_MEMORY_METASPACE_CHUNKHEADERPOOL_HPP

#include "memory/allocation.hpp"
#include "memory/metaspace/counters.hpp"
#include "memory/metaspace/metachunk.hpp"
#include "memory/metaspace/metachunkList.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"

namespace metaspace {

// Chunk headers (Metachunk objects) are separate entities from their payload.
// 由于它们在伙伴分配过程中经常被分配和释放
// （拆分、合并块经常发生）我们希望快速分配它们. 因此，我们把它们放在一个简单的池子里 (有点像私有的slab分配器).
// todo 元空间: ChunkHeaderPool
// 申请 MetaChunk 用于描述内存：

// 首先查看 _freelist，是否有之前放回的 MetaChunk 可以使用，如果有，就返回那个 MetaChunk，并从 _freelist 移除这个 MetaChunk
// 如果没有，读取 _current_slab 指向的 Slab，Slab 核心就是一个预分配好的 MetaChunk 数组（大小是 128），_top 指的是当前使用到数组的哪一个。
// 如果 _top 没有到 128，返回 _top 代表的 MetaChunk，并将 _top 加 1。
// 如果 _top 到 128，创建新的 Slab，_current_slab 指向这个新的 Slab

// 回收 MetaChunk：放入 _freelist
class ChunkHeaderPool : public CHeapObj<mtMetaspace> {

  static const int SlabCapacity = 128;

  struct Slab : public CHeapObj<mtMetaspace> {
    Slab* _next;
    int _top;
    Metachunk _elems [SlabCapacity];
    Slab() : _next(nullptr), _top(0) {
      for (int i = 0; i < SlabCapacity; i++) {
        _elems[i].clear();
      }
    }
  };

  IntCounter _num_slabs;
  Slab* _first_slab;
  Slab* _current_slab;

  IntCounter _num_handed_out;

  MetachunkList _freelist;

  void allocate_new_slab();

  static ChunkHeaderPool* _chunkHeaderPool;

public:

  ChunkHeaderPool();

  ~ChunkHeaderPool();

  // Allocates a Metachunk structure. The structure is uninitialized.
  Metachunk* allocate_chunk_header() {
    DEBUG_ONLY(verify());

    Metachunk* c = nullptr;
    c = _freelist.remove_first();
    assert(c == nullptr || c->is_dead(), "Not a freelist chunk header?");
    if (c == nullptr) {
      if (_current_slab == nullptr ||
          _current_slab->_top == SlabCapacity) {
        allocate_new_slab();
        assert(_current_slab->_top < SlabCapacity, "Sanity");
      }
      c = _current_slab->_elems + _current_slab->_top;
      _current_slab->_top++;
    }
    _num_handed_out.increment();
    // By contract, the returned structure is uninitialized.
    // Zap to make this clear.
    DEBUG_ONLY(c->zap_header(0xBB);)

    return c;
  }

  void return_chunk_header(Metachunk* c) {
    // We only ever should return free chunks, since returning chunks
    // happens only on merging and merging only works with free chunks.
    assert(c != nullptr && c->is_free(), "Sanity");
#ifdef ASSERT
    // In debug, fill dead header with pattern.
    c->zap_header(0xCC);
    c->set_next(nullptr);
    c->set_prev(nullptr);
#endif
    c->set_dead();
    _freelist.add(c);
    _num_handed_out.decrement();
  }

  // Returns number of allocated elements.
  int used() const                   { return _num_handed_out.get(); }

  // Returns number of elements in free list.
  int freelist_size() const          { return _freelist.count(); }

  // Returns size of memory used.
  size_t memory_footprint_words() const;

  DEBUG_ONLY(void verify() const;)

  static void initialize();

  // Returns reference to the one global chunk header pool.
  static ChunkHeaderPool* pool() { return _chunkHeaderPool; }

};

} // namespace metaspace

#endif // SHARE_MEMORY_METASPACE_CHUNKHEADERPOOL_HPP
