#ifndef APPS_CPU_RELAX_HPP
#define APPS_CPU_RELAX_HPP

namespace apps {

// Hint to the CPU that we are spinning on a value another core will change.
//
// Without it an empty poll loop re-reads the queue's write_pos at full rate,
// which maximizes coherence traffic and lengthens the publisher's store - the
// spin actively slows down the thing it is waiting for.
inline void cpuRelax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("isb" ::: "memory");
#else
    // No hint available; the spin is still correct, just noisier.
#endif
}

} // namespace apps

#endif // APPS_CPU_RELAX_HPP
