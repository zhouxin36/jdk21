hotspot/src/share/vm/gc_implementation/g1/g1CollectedHeap.cpp
resize_if_necessary_after_full_collection(size_t word_size) {
    const size_t used_after_gc = used();
    const size_t capacity_after_gc = capacity();
    const size_t free_after_gc = capacity_after_gc - used_after_gc;
    const double minimum_free_percentage = (double) MinHeapFreeRatio / 100.0;
    const double maximum_used_percentage = 1.0 - minimum_free_percentage;
    const double maximum_free_percentage = (double) MaxHeapFreeRatio / 100.0;
    const double minimum_used_percentage = 1.0 - maximum_free_percentage;
    const size_t min_heap_size = collector_policy()->min_heap_byte_size();
    const size_t max_heap_size = collector_policy()->max_heap_byte_size();
    double used_after_gc_d = (double) used_after_gc;
    double minimum_desired_capacity_d = used_after_gc_d / maximum_used_percentage;
    double maximum_desired_capacity_d = used_after_gc_d / minimum_used_percentage;
    double desired_capacity_upper_bound = (double) max_heap_size;
    minimum_desired_capacity_d = MIN2(minimum_desired_capacity_d,
                                      desired_capacity_upper_bound);
    maximum_desired_capacity_d = MIN2(maximum_desired_capacity_d,
                                      desired_capacity_upper_bound);
    size_t minimum_desired_capacity = (size_t) minimum_desired_capacity_d;
    size_t maximum_desired_capacity = (size_t) maximum_desired_capacity_d;
    minimum_desired_capacity = MIN2(minimum_desired_capacity, max_heap_size);
    maximum_desired_capacity =  MAX2(maximum_desired_capacity, min_heap_size);
    if (capacity_after_gc < minimum_desired_capacity) {
        size_t expand_bytes = minimum_desired_capacity - capacity_after_gc;
        expand(expand_bytes);
    } else if (capacity_after_gc > maximum_desired_capacity) {
        size_t shrink_bytes = capacity_after_gc - maximum_desired_capacity;
        shrink(shrink_bytes);
    }
}

