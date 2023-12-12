/*
 * Copyright (c) 2015, 2022, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_GC_G1_G1COLLECTORSTATE_HPP
#define SHARE_GC_G1_G1COLLECTORSTATE_HPP

#include "gc/g1/g1GCPauseType.hpp"
#include "utilities/globalDefinitions.hpp"

// State of the G1 collection.
class G1CollectorState {
  // Indicates whether we are in the phase where we do partial gcs that only contain
  // the young generation. Not set while _in_full_gc is set.
  bool _in_young_only_phase;

  // Indicates whether we are in the last young gc before the mixed gc phase. This GC
  // is required to keep pause time requirements.
  bool _in_young_gc_before_mixed;

  // If _initiate_conc_mark_if_possible is set at the beginning of a
  // pause, it is a suggestion that the pause should start a marking
  // cycle by doing the concurrent start work. However, it is possible
  // that the concurrent marking thread is still finishing up the
  // previous marking cycle (e.g., clearing the marking bitmap).
  // If that is the case we cannot start a new cycle and
  // we'll have to wait for the concurrent marking thread to finish
  // what it is doing. In this case we will postpone the marking cycle
  // initiation decision for the next pause. When we eventually decide
  // to start a cycle, we will set _in_concurrent_start_gc which
  // will stay true until the end of the concurrent start pause doing the
  // concurrent start work.
  volatile bool _in_concurrent_start_gc;

  // 如果在暂停开始时设置了_initiate_conc_mark_if_possible，则建议暂停应通过执行并发启动工作来启动标记周期。
  // 但是，并发标记线程可能仍在完成上一个标记周期（例如，清除标记位图）。
  // 如果是这种情况，我们就无法开始新的循环，我们将不得不等待并发标记线程完成它正在做的事情。
  // 在这种情况下，我们将推迟标记周期的启动决定，以备下一次暂停。
  // 当我们最终决定开始一个循环时，我们将设置_in_concurrent_start_gc该设置将保持true不变，直到并发启动暂停结束，执行并发启动工作。
  volatile bool _initiate_conc_mark_if_possible;

  // Marking or rebuilding remembered set work is in progress. Set from the end
  // of the concurrent start pause to the end of the Cleanup pause.
  bool _mark_or_rebuild_in_progress;

  // The marking bitmap is currently being cleared or about to be cleared.
  bool _clearing_bitmap;

  // Set during a full gc pause.
  bool _in_full_gc;

public:
  G1CollectorState() :
    _in_young_only_phase(true),
    _in_young_gc_before_mixed(false),

    _in_concurrent_start_gc(false),
    _initiate_conc_mark_if_possible(false),

    _mark_or_rebuild_in_progress(false),
    _clearing_bitmap(false),
    _in_full_gc(false) { }

  // Phase setters
  void set_in_young_only_phase(bool v) { _in_young_only_phase = v; }

  // Pause setters
  void set_in_young_gc_before_mixed(bool v) { _in_young_gc_before_mixed = v; }
  void set_in_concurrent_start_gc(bool v) { _in_concurrent_start_gc = v; }
  void set_in_full_gc(bool v) { _in_full_gc = v; }

  void set_initiate_conc_mark_if_possible(bool v) { _initiate_conc_mark_if_possible = v; }

  void set_mark_or_rebuild_in_progress(bool v) { _mark_or_rebuild_in_progress = v; }
  void set_clearing_bitmap(bool v) { _clearing_bitmap = v; }

  // Phase getters
  bool in_young_only_phase() const { return _in_young_only_phase && !_in_full_gc; }
  bool in_mixed_phase() const { return !_in_young_only_phase && !_in_full_gc; }

  // Specific pauses
  bool in_young_gc_before_mixed() const { return _in_young_gc_before_mixed; }
  bool in_full_gc() const { return _in_full_gc; }
  bool in_concurrent_start_gc() const { return _in_concurrent_start_gc; }

  bool initiate_conc_mark_if_possible() const { return _initiate_conc_mark_if_possible; }

  bool mark_or_rebuild_in_progress() const { return _mark_or_rebuild_in_progress; }
  bool clearing_bitmap() const { return _clearing_bitmap; }

  // Calculate GC Pause Type from internal state.
  G1GCPauseType young_gc_pause_type(bool concurrent_operation_is_full_mark) const;
};

#endif // SHARE_GC_G1_G1COLLECTORSTATE_HPP
