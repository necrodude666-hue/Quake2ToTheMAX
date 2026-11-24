#ifndef SOFTWARE_SMP_H
#define SOFTWARE_SMP_H

enum {
 SW_SMP_OFF, SW_SMP_AUTO, SW_SMP_FORCE, SW_SMP_BALANCED, SW_SMP_ADAPTIVE,
 SW_SMP_CACHE_WORKER, SW_SMP_AUDIO, SW_SMP_ADAPTIVE_AUDIO, SW_SMP_FULL_AUDIO, SW_SMP_COUNT
};

#define SW_SMP_NAMES "Off", "Auto", "Forced Auto", "Balanced cache", "Adaptive cache", \
                     "Cache on worker", "Audio PCM", "Adaptive + PCM", "Full Audio Worker"

#endif
