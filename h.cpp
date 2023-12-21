hotspot/src/share/vm/runtime/interfaceSupport.hpp
static inline void transition_from_native(JavaThread *thread,
                                          JavaThreadState to) {
    thread->set_thread_state(_thread_in_native_trans);
    // 使用屏障，确得GC线程读到最好状态
    if (os::is_MP()) {
        if (UseMembar) {
            OrderAccess::fence();
        } else {
            InterfaceSupport::serialize_memory(thread);
        }
    }
    if (SafepointSynchronize::do_call_back() ||
        thread->is_suspend_after_native()) {
        JavaThread::check_safepoint_and_suspend_for_native_trans(thread);
        CHECK_UNHANDLED_OOPS_ONLY(thread->clear_unhandled_oops();)
    }
    thread->set_thread_state(to);
}

otspot/src/os_cpu/linux_x86/vm/orderAccess_linux_x86.inline.hpp
inline void OrderAccess::fence() {
    if (os::is_MP()) {
        // 使用指令locked addl因为有时候mfence指令的CPU花费更高
#ifdef AMD64
        __asm__ volatile ("lock; addl $0,0(%%rsp)" : : : "cc", "memory");
#else
        __asm__ volatile ("lock; addl $0,0(%%esp)" : : : "cc", "memory");
#endif
    }
}

