/*
 * Copyright (c) 2012, 2023, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2017, 2020 SAP SE. All rights reserved.
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

#ifndef SHARE_MEMORY_METASPACE_METACHUNK_HPP
#define SHARE_MEMORY_METASPACE_METACHUNK_HPP

#include "memory/metaspace/chunklevel.hpp"
#include "memory/metaspace/counters.hpp"
#include "utilities/debug.hpp"
#include "utilities/globalDefinitions.hpp"

class outputStream;

namespace metaspace {

class VirtualSpaceNode;

// Metachunk 是一个连续的元空间内存区域.它被 MetaspaceArena 用于从通过指针碰撞进行分配 (有点类似于 java 堆中的 TLAB.
//
// Metachunk 对象本身（“chunk header”）与它所描述的内存区域（chunk payload）是分开的. 它也可以没有有效载荷（“dead” chunk）.
// 就其本身而言，它存在于 C 堆中, managed as part of a pool of Metachunk headers (ChunkHeaderPool).
//
// +---------+                 +---------+                 +---------+
// |MetaChunk| <--next/prev--> |MetaChunk| <--next/prev--> |MetaChunk|   Chunk headers
// +---------+                 +---------+                 +---------+   in C-heap
//     |                           |                          |
//    base                        base                       base
//     |                          /                           |
//    /            ---------------                           /
//   /            /              ----------------------------
//  |            |              /
//  v            v              v
// +---------+  +---------+    +-------------------+
// |         |  |         |    |                   |
// |  chunk  |  |  chunk  |    |      chunk        |    The real chunks ("payload")
// |         |  |         |    |                   |    live in Metaspace
// +---------+  +---------+    +-------------------+
//
//
// -- Metachunk state --
//
// A Metachunk is "in-use" if 它是 MetaspaceArena 的一部分. 这意味着它的内存被使用了 - 或即将使用 - 代表class loader 保存 VM 元数据.
//
// A Metachunk is "free" if 其有效载荷当前未使用.在这种情况下，它由一个chunk freelist管理 (the ChunkManager).
//
// A Metachunk is "dead" if 它没有相应的有效载荷.
//  In that case it lives as part of a freelist-of-dead-chunk-headers in the ChunkHeaderPool.
//
// Metachunk 始终是链表的一部分.
// In-use chunks are part of the chunk list of a MetaspaceArena.
// Free chunks are in a freelist in the ChunkManager.
// Dead chunk headers are in a linked list as part of the ChunkHeaderPool.
//
//
// -- Level --
//
// Metachunks 作为伙伴式分配方案的一部分进行管理.
// 尺寸始终以 2 的幂为步长,
// 范围从最小的块大小 （1Kb） 到最大的块大小 （16Mb） (see chunklevel.hpp).
// Its size is encoded as level, with level 0 being the largest chunk size ("root chunk").
//
//
// -- Payload commit state --
//
// Metachunk 有效负载（“真实块”）可以是已提交的、部分已提交的或完全未提交的。
// 从技术上讲，有效负载可能是“方格”提交的 - i.e. 已提交和未提交的部分可能会交错 -
// 但重要的部分是从有效载荷的底部开始承诺了多少连续空间 (因为那是我们分配的地方).
//
// Metachunk 跟踪从有效负载底部开始提交的空间量
// - 这是一种性能优化
// - 而底层（VirtualSpaceNode->commitmask）则跟踪“真实”提交状态, 又名哪些颗粒被提交,
//  independent on what chunks reside above those granules.

//            +--------------+ <- end    -----------+ ----------+
//            |              |                      |           |
//            |              |                      |           |
//            |              |                      |           |
//            |              |                      |           |
//            |              |                      |           |
//            | -----------  | <- committed_top  -- +           |
//            |              |                      |           |
//            |              |                      | "free"    |
//            |              |                      |           | size
//            |              |     "free_below_     |           |
//            |              |        committed"    |           |
//            |              |                      |           |
//            |              |                      |           |
//            | -----------  | <- top     --------- + --------  |
//            |              |                      |           |
//            |              |     "used"           |           |
//            |              |                      |           |
//            +--------------+ <- start   ----------+ ----------+
//
//
// -- Relationships --
//
// Chunks are managed by a binary buddy style allocator
//  (see https://en.wikipedia.org/wiki/Buddy_memory_allocation).
// 不是root chunk的chunk总是有一个相邻的伙伴.
// 第一个 chunk 叫 leader, 第二个follower.
//
// +----------+----------+
// | leader   | follower |
// +----------+----------+
//
//
// -- Layout in address space --
//
// 为了实现伙伴样式的分配，我们需要一种简单的方法从一个块到代表相邻块的 Metachunk
//  (preceding resp. following it in memory).
// 但是 Metachunk header 和chunk在物理上是分开的, 并且不可能从块的开头获取 Metachunk*。
// 因此，Metachunk header 是第二个链表的一部分,
// 描述其有效负载在内存中出现的顺序:
//
// +---------+                       +---------+                       +---------+
// |MetaChunk| <--next/prev_in_vs--> |MetaChunk| <--next/prev_in_vs--> |MetaChunk|
// +---------+                       +---------+                       +---------+
//     |                                 |                                  |
//    base                              base                               base
//     |                                 /                                  |
//    /        --------------------------                                  /
//   /        /          --------------------------------------------------
//  |         |         /
//  v         v         v
// +---------+---------+-------------------+
// |  chunk  |  chunk  |      chunk        |
// +---------+---------+-------------------+
//
// todo 元空间: Metachunk
class Metachunk {

  // start of chunk memory; null if dead.
  MetaWord* _base;

  // Used words.
  size_t _used_words;

  // Size of the region, starting from base, which is guaranteed to be committed. In words.
  //  The actual size of committed regions may actually be larger.
  //
  //  (This is a performance optimization. The underlying VirtualSpaceNode knows
  //   which granules are committed; but we want to avoid having to ask.)
  size_t _committed_words;

  chunklevel_t _level; // aka size.

  // state_free:    free, owned by a ChunkManager
  // state_in_use:  in-use, owned by a MetaspaceArena
  // dead:          just a hollow chunk header without associated memory, owned
  //                 by chunk header pool.
  enum class State : uint8_t {
    Free = 0,
    InUse = 1,
    Dead = 2
  };
  State _state;

  // 不幸的是，我们需要一个指向VirtualSpaceNode的反向链接
  // 用于拆分和合并节点。
  VirtualSpaceNode* _vsnode;

  // A chunk header is kept in a list:
  // 1 in the list of used chunks inside a MetaspaceArena, if it is in use
  // 2 in the list of free chunks inside a ChunkManager, if it is free
  // 3 in the freelist of unused headers inside the ChunkHeaderPool,
  //   if it is unused (e.g. result of chunk merging) and has no associated
  //   memory area.
  Metachunk* _prev;
  Metachunk* _next;

  // Furthermore, we keep, per chunk, information about the neighboring chunks.
  // This is needed to split and merge chunks.
  //
  // Note: These members can be modified concurrently while a chunk is alive and in use.
  // This can happen if a neighboring chunk is added or removed.
  // This means only read or modify these members under expand lock protection.
  Metachunk* _prev_in_vs;
  Metachunk* _next_in_vs;

  // Commit uncommitted section of the chunk.
  // Fails if we hit a commit limit.
  bool commit_up_to(size_t new_committed_words);

  DEBUG_ONLY(static void assert_have_expand_lock();)

public:

  Metachunk() :
    _base(nullptr),
    _used_words(0),
    _committed_words(0),
    _level(chunklevel::ROOT_CHUNK_LEVEL),
    _state(State::Free),
    _vsnode(nullptr),
    _prev(nullptr), _next(nullptr),
    _prev_in_vs(nullptr),
    _next_in_vs(nullptr)
  {}

  void clear() {
    _base = nullptr;
    _used_words = 0; _committed_words = 0;
    _level = chunklevel::ROOT_CHUNK_LEVEL;
    _state = State::Free;
    _vsnode = nullptr;
    _prev = nullptr; _next = nullptr;
    _prev_in_vs = nullptr; _next_in_vs = nullptr;
  }

  size_t word_size() const        { return chunklevel::word_size_for_level(_level); }

  MetaWord* base() const          { return _base; }
  MetaWord* top() const           { return base() + _used_words; }
  MetaWord* committed_top() const { return base() + _committed_words; }
  MetaWord* end() const           { return base() + word_size(); }

  // Chunk list wiring
  void set_prev(Metachunk* c)     { _prev = c; }
  Metachunk* prev() const         { return _prev; }
  void set_next(Metachunk* c)     { _next = c; }
  Metachunk* next() const         { return _next; }

  DEBUG_ONLY(bool in_list() const { return _prev != nullptr || _next != nullptr; })

  // Physical neighbors wiring
  void set_prev_in_vs(Metachunk* c) { DEBUG_ONLY(assert_have_expand_lock()); _prev_in_vs = c; }
  Metachunk* prev_in_vs() const     { DEBUG_ONLY(assert_have_expand_lock()); return _prev_in_vs; }
  void set_next_in_vs(Metachunk* c) { DEBUG_ONLY(assert_have_expand_lock()); _next_in_vs = c; }
  Metachunk* next_in_vs() const     { DEBUG_ONLY(assert_have_expand_lock()); return _next_in_vs; }

  bool is_free() const            { return _state == State::Free; }
  bool is_in_use() const          { return _state == State::InUse; }
  bool is_dead() const            { return _state == State::Dead; }
  void set_free()                 { _state = State::Free; }
  void set_in_use()               { _state = State::InUse; }
  void set_dead()                 { _state = State::Dead; }

  // Return a single char presentation of the state ('f', 'u', 'd')
  char get_state_char() const;

  void inc_level()                { _level++; DEBUG_ONLY(chunklevel::is_valid_level(_level);) }
  void dec_level()                { _level --; DEBUG_ONLY(chunklevel::is_valid_level(_level);) }
  chunklevel_t level() const      { return _level; }

  // Convenience functions for extreme levels.
  bool is_root_chunk() const      { return chunklevel::ROOT_CHUNK_LEVEL == _level; }
  bool is_leaf_chunk() const      { return chunklevel::HIGHEST_CHUNK_LEVEL == _level; }

  VirtualSpaceNode* vsnode() const        { return _vsnode; }

  size_t used_words() const                   { return _used_words; }
  size_t free_words() const                   { return word_size() - used_words(); }
  size_t free_below_committed_words() const   { return committed_words() - used_words(); }
  void reset_used_words()                     { _used_words = 0; }

  size_t committed_words() const      { return _committed_words; }
  void set_committed_words(size_t v);
  bool is_fully_committed() const     { return committed_words() == word_size(); }
  bool is_fully_uncommitted() const   { return committed_words() == 0; }

  // Ensure that chunk is committed up to at least new_committed_words words.
  // Fails if we hit a commit limit.
  bool ensure_committed(size_t new_committed_words);
  bool ensure_committed_locked(size_t new_committed_words);

  // Ensure that the chunk is committed far enough to serve an additional allocation of word_size.
  bool ensure_committed_additional(size_t additional_word_size)   {
    return ensure_committed(used_words() + additional_word_size);
  }

  // Uncommit chunk area. The area must be a common multiple of the
  // commit granule size (in other words, we cannot uncommit chunks smaller than
  // a commit granule size).
  void uncommit();
  void uncommit_locked();

  // Allocation from a chunk

  // Allocate word_size words from this chunk (word_size must be aligned to
  //  allocation_alignment_words).
  //
  // Caller must make sure the chunk is both large enough and committed far enough
  // to hold the allocation. Will always work.
  //
  MetaWord* allocate(size_t request_word_size);

  // Initialize structure for reuse.
  void initialize(VirtualSpaceNode* node, MetaWord* base, chunklevel_t lvl) {
    clear();
    _vsnode = node; _base = base; _level = lvl;
  }

  // Returns true if this chunk is the leader in its buddy pair, false if not.
  // Do not call for root chunks.
  bool is_leader() const {
    assert(!is_root_chunk(), "Root chunks have no buddy."); // Bit harsh?
    return is_aligned(base(), chunklevel::word_size_for_level(level() - 1) * BytesPerWord);
  }

  //// Debug stuff ////
#ifdef ASSERT
  void verify() const;
  // Verifies linking with neighbors in virtual space. Needs expand lock protection.
  void verify_neighborhood() const;
  void zap_header(uint8_t c = 0x17);

  // Returns true if given pointer points into the payload area of this chunk.
  bool is_valid_pointer(const MetaWord* p) const {
    return base() <= p && p < top();
  }

  // Returns true if given pointer points into the committed payload area of this chunk.
  bool is_valid_committed_pointer(const MetaWord* p) const {
    return base() <= p && p < committed_top();
  }

#endif // ASSERT

  void print_on(outputStream* st) const;

};

// Little print helpers: since we often print out chunks, here some convenience macros
#define METACHUNK_FORMAT                "@" PTR_FORMAT ", %c, base " PTR_FORMAT ", level " CHKLVL_FORMAT
#define METACHUNK_FORMAT_ARGS(chunk)    p2i(chunk), chunk->get_state_char(), p2i(chunk->base()), chunk->level()

#define METACHUNK_FULL_FORMAT                "@" PTR_FORMAT ", %c, base " PTR_FORMAT ", level " CHKLVL_FORMAT " (" SIZE_FORMAT "), used: " SIZE_FORMAT ", committed: " SIZE_FORMAT ", committed-free: " SIZE_FORMAT
#define METACHUNK_FULL_FORMAT_ARGS(chunk)    p2i(chunk), chunk->get_state_char(), p2i(chunk->base()), chunk->level(), chunk->word_size(), chunk->used_words(), chunk->committed_words(), chunk->free_below_committed_words()

} // namespace metaspace

#endif // SHARE_MEMORY_METASPACE_METACHUNK_HPP
