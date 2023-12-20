jdk10u/src/Hotspot/share/gc/g1/g1FullCollector.cpp
void G1FullCollector::complete_collection() {
/*在进行并行标记的时候，会把对象的对象头存放起来，此时把它们都恢复。注意这个地方存储对象头信息的数据结构实际上是一个map，就是对象和对象头的信息。当经过上述压缩过程，这个对象的地址当然也就更新了，所以可以直接恢复。*/
    restore_marks();
    // 这是为了C2的优化，因为对象的位置发生了变化，所以必须更新对象派生关系的地址
    update_derived_pointers();
    // 恢复偏向锁的信息
    BiasedLocking::restore_marks();
    // 做各种后处理，更新新生代的长度等
    CodeCache::gc_epilogue();
    JvmtiExport::gc_epilogue();
    _heap->prepare_heap_for_mutators();
    _heap->g1_policy()->record_full_collection_end();
}

