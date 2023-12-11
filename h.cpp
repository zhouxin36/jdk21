//根扫描结束之后，就进入了并发标记子阶段，具体在ConcurrentMark::markFromRoots()中，
// 它和我们前面提到的scanFromRoots()非常类似。
// 我们只看标记的具体工作CMConcurrentMarkingTask::work，代码如下所示：

//hotspot/src/share/vm/gc_implementation/g1/concurrentMark.cpp
void CMConcurrentMarkingTask::work(uint worker_id) {
    // 当发生同步时，进行等待，否则继续。关于同步的使用在第10章介绍
    SuspendibleThreadSet::join();
    CMTask* the_task = _cm->task(worker_id);
    if (!_cm->has_aborted()) {
        do {
            // 设置标记目标时间，G1ConcMarkStepDurationMillis默认值是10ms，
            // 表示并发标记子阶段在10ms内完成。
            double mark_step_duration_ms = G1ConcMarkStepDurationMillis;
            the_task->do_marking_step(mark_step_duration_ms,
                                      true  /* do_termination */,
                                      false /* is_serial*/);
            _cm->clear_has_overflown();
            _cm->do_yield_check(worker_id);
            // CM任务结束后还可以睡眠一会
        } while (!_cm->has_aborted() && the_task->has_aborted());
    }
    SuspendibleThreadSet::leave();
    _cm->update_accum_task_vtime(worker_id, end_vtime - start_vtime);
}
//具体的处理在do_marking_step中，主要包含两步：
//·处理SATB缓存。
//·根据已经标记的分区nextMarkBitmap的对象进行处理，处理的方式是针对已标记对象的每一个field进行递归并发标记。
//代码如下所示
//hotspot/src/share/vm/gc_implementation/g1/concurrentMark.cpp
void CMTask::do_marking_step(double time_target_ms,
                             bool do_termination,
                             bool is_serial) {
    // 根据过去运行的标记信息，预测本次标记要花费的时间
    double diff_prediction_ms =  g1_policy->get_new_prediction(&_marking_
    step_diffs_ms);
    _time_target_ms = time_target_ms - diff_prediction_ms;
    // 这里设置的closure会在后面用到
    CMBitMapClosure bitmap_closure(this, _cm, _nextMarkBitMap);
    G1CMOopClosure  cm_oop_closure(_g1h, _cm, this);
    set_cm_oop_closure(&cm_oop_closure);
    // 处理SATB队列
    drain_satb_buffers();
    // 根据根对象标记时发现的对象开始处理。在上面已经介绍过。
    drain_local_queue(true);
    // 针对全局标记栈开始处理，注意这里为了效率，只有当全局标记栈超过1/3才会开始处理。
    // 处理的思路很简单，就是把全局标记栈的对象移入CMTask的队列中，等待处理。
    drain_global_stack(true);
    do {
        if (!has_aborted() && _curr_region != NULL) {
            ……
            // 这个MemRegion是新增的对象，所以从finger开始到结束全部开始标记
            MemRegion mr = MemRegion(_finger, _region_limit);
            if (mr.is_empty()) {
                giveup_current_region();
                regular_clock_call();
            } else if (_curr_region->isHumongous() && mr.start() == _curr_
                region->bottom()) {
                /*如果是大对象，并且该分区是该对象的最后一个分区，则：1）如果对象被标记，说明这个对象需要被作为灰对象处理。处理在CMBitMapClosure::do_bit 中。2）对象没有标记，直接结束本分区。*/
                if (_nextMarkBitMap->isMarked(mr.start())) {
                    BitMap::idx_t offset = _nextMarkBitMap->heapWordToOffset(mr.start());
                    bitmap_closure.do_bit(offset);
                    /*do_bit所做的事情有：
                     1）调整finger，处理本对象（准确地说是处理对象的Field所指向的oop对象），处理是调用process_grey_object<true>(obj)，所以实际上形成递归
                     2）然后处理本地队列。
                     3）处理全局标记栈。*/
                }
                giveup_current_region();
                regular_clock_call();
            } else if (_nextMarkBitMap->iterate(&bitmap_closure, mr)) {
                // 处理本分区的标记对象，这里会对整个分区里面的对象调用CMBitMapClosure::do_bit
                //完成标记，实际上形成递归。
                giveup_current_region();
                regular_clock_call();
            } else {
            }
        }
        // 再次处理本地队列和全局标记栈。实际上这是为了后面的加速，标记发生时，会有新的对象进来。
        drain_local_queue(true);
        drain_global_stack(true);
        while (!has_aborted() && _curr_region == NULL && !_cm->out_of_regions()) {
            /*标记本分区已经被处理，这时可以修改全局的finger。注意，在这里是每个线程都将获得分区，获取的逻辑在claim_region：所有的CM线程都去竞争全局finger指向的分区（使用CAS），并设置全局finger到下一个分区的起始位置。当所有的分区都遍历完了之后，即全局finger到达整个堆空间的最后，这时claimed_region就会为NULL，也就是说NULL表示所有的分区都处理完了。*/
            HeapRegion* claimed_region = _cm->claim_region(_worker_id);
            setup_for_region(claimed_region);
        }
// 这个循环会继续，只要分区不为NULL，并且没有被终止，这就是前面调用giveup_current_region
// 和regular_clock_call的原因，就是为了中止循环。
    } while ( _curr_region != NULL && !has_aborted());
    if (!has_aborted()) {
        // 再处理一次SATB缓存，那么再标记的时候工作量就少了
        drain_satb_buffers();
    }
    // 这个时候需要把本地队列和全局标记栈全部处理掉。
    drain_local_queue(false);
    drain_global_stack(false);
    // 尝试从其他的任务的队列中偷窃任务，这是为了更好的性能
    if (do_stealing && !has_aborted()) {
        while (!has_aborted()) {
            oop obj;
            if (_cm->try_stealing(_worker_id, &_hash_seed, obj)) {
                scan_object(obj);
                drain_local_queue(false);
                drain_global_stack(false);
            } else {
                break;
            }
        }
    }
}

//其中SATB的处理在drain_satb_buffers中，代码如下所示
//hotspot/src/share/vm/gc_implementation/g1/concurrentMark.cpp
void CMTask::drain_satb_buffers() {
    CMSATBBufferClosure satb_cl(this, _g1h);
    SATBMarkQueueSet& satb_mq_set = JavaThread::satb_mark_queue_set();
    // 因为CM线程和Mutator并发运行，所以Mutator的SATB不断地变化，这里只对放入queue
    // set中的SATB队列处理。
    while (!has_aborted() &&  satb_mq_set.apply_closure_to_completed_buffer
            (&satb_cl)) {
        regular_clock_call();
    }
    // 因为标记需要对老生代进行，可能要花费的时间比较多，所以增加了标记检查，如果发现有溢出，
    // 终止。线程同步等满足终止条件的情况都会设置停止标志来终止标记动作。
    decrease_limits();
}

//SATB队列的结构和前面提到的dirty card队列非常类似，处理方式也非常类似。所以只需要关注不同点：
//·SATB队列的长度为1k，是由参数G1SATBBufferSize控制，表示每个队列有1000个对象。
//·SATB缓存针对每个队列有一个参数G1SATBBufferEnqueueingThresholdPercent（默认值是60），
//      表示当一个队列满了之后，首先进行过滤处理，过滤后如果使用率超过这个阈值则新分配一个队列，否则重用这个队列。
//      过滤的条件就是这个对象属于新分配对象（位于NTAMS之下），且还没有标记，后续会处理该对象。
//这里的处理逻辑主要是在CMSATBBufferClosure::do_entry中。
// 调用路径从SATBMarkQueueSet::apply_closure_to_completed_buffer到CMSATBBufferClosure::do_buffer
// 再到CMSATBBufferClosure::do_entry。源码中似乎是有一个bug，
// 在SATBMarkQueueSet::apply_closure_to_completed_buffer和CMSATBBufferClosure::do_buffer里面都是用循环处理，
// 这将导致一个队列被处理两次。实际上这是为了修复一个bug引入的特殊处理（这个bug和humongous对象的处理有关）。
//这里需要提示一点，因为SATB set是一个全局的变量，所以使用的时候会使用锁，每个CMTask用锁摘除第一元素后就可以释放锁了。
//CMSATBBufferClosure::do_entry的代码如下所示：
//hotspot/src/share/vm/gc_implementation/g1/concurrentMark.cpp
void CMSATBBufferClosure::do_entry(void* entry) const {
    HeapRegion* hr = _g1h->heap_region_containing_raw(entry);
    if (entry < hr->next_top_at_mark_start()) {
        oop obj = static_cast<oop>(entry);
        _task->make_reference_grey(obj, hr);
    }
}

//主要工作在make_reference_grey中，代码如下所示：
hotspot/src/share/vm/gc_implementation/g1/concurrentMark.cpp
void CMTask::make_reference_grey(oop obj, HeapRegion* hr) {
    // 对对象进行标记和计数
    if (_cm->par_mark_and_count(obj, hr, _marked_bytes_array, _card_bm)) {
        HeapWord* global_finger = _cm->finger();
        if (is_below_finger(obj, global_finger)) {
            // 对象是一个原始（基本类型）的数组，无须继续追踪。直接记录对象的长度
            if (obj->is_typeArray()) {
                // 这里传入的模版参数为false，说明对象是基本对象，意味着没有field要处理。
                process_grey_object<false>(obj);
            } else {
                // ①
                push(obj);
            }
        }
    }
}

//在上面代码①处，把对象入栈，这个对象可能是objArray、array、instance、instacneRef、instacneMirror等，等待后续处理。
//在后续的处理中，实际上通过G1CMOopClosure::do_oop最终会调用的是process_grey_object<true>(obj)，
//        该方法对obj标记同时处理每一个字段。
//这个处理实际上就是对对象遍历，然后对每一个field标记处理。
//和前面在copy_to_surivivor_space中稍有不同，那个方法是对象从最后一个字段复制，这里是从第一个字段开始标记。
//为什么？这里需要考虑push对应的队列大小。
//在这里队列的大小固定默认值是16k（32位JVM）或者128k（64位JVM）。
//几个队列会组成一个queue set，这个集合的大小和ParallelGCThreads一致。
//当push对象到队列中时，可能会发生溢出（即超过CMTask中队列的最大值），
//这时候需要把CMTask中的待处理对象（这里就是灰色对象）放入到全局标记栈（globalmark stack）中。
//这个全局标记栈的大小可以通过参数设置。
//这里有两个参数分别为：MarkStackSize和MarkStackSizeMax，在32位JVM中设置为32k和4M，64位中设置为4M和512M。
//如果没有设置G1可以启发式推断，确保MarkStackSize最小为32k（或者和并发线程参数ParallelGCThreads正相关，
//    如ParallelGCThreads=8，则32位JVM中MarkStackSize=8×16k=128K，其中16k是队列的大小）。
//全局标记栈仍然可能发生溢出，当溢出发生时会做两个事情：
//·设置标记终止，并在合适的时机终止本任务（CMTask）的标记动作。
//·尝试去扩展全局标记栈。
//最后再谈论一下finger，实际上有两个finger：一个是全局的，一个是每个CMTask中的finger。
//全局的finger在CM初始化时是分区的起始地址。随着对分区的处理，这个finger会随之调整。
//简单地说在finger之下的地址都认为是新加入的对象，认为是活跃对象。
//局部的finger指的是每个CMTask的nextMarkBitMap指向的起始位置，在这个位置之下也说明该对象是新加入的，还是活跃对象。
//引入局部finger可以并发处理，加快速度。
