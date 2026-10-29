#include <stdint.h>
#if defined(_WIN32)
// clang-format off: bcrypt.h requires Windows declarations first.
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// The only native part of std.random. No allocation and no shared PRNG state.
uint64_t rh_random_seed(uint64_t *state)
{
#if defined(RH_TEST_SEED_FAILURE)
    (void)state;
    return 0;
#elif defined(_WIN32)
    return BCryptGenRandom(NULL, (PUCHAR)state, sizeof(*state), BCRYPT_USE_SYSTEM_PREFERRED_RNG) ==
           0;
#else
    int source;
    do {
        source = open("/dev/urandom", O_RDONLY);
    } while (source < 0 && errno == EINTR);
    if (source < 0) {
        return 0;
    }
    size_t filled = 0;
    while (filled < sizeof(*state)) {
        ssize_t n = read(source, (unsigned char *)state + filled, sizeof(*state) - filled);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            close(source);
            return 0;
        }
        filled += (size_t)n;
        if (filled == sizeof(*state)) {
            break;
        }
    }
    close(source);
    return 1;
#endif
}
