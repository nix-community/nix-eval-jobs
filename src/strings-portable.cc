#include <cstring>

#ifdef __APPLE__
// for sys_siglist and sys_errlist
#include <cstdio>
#include <csignal>
#elif defined(__FreeBSD__)
#include <csignal>
#endif

#if defined(__GLIBC__)
#include <string.h> //NOLINT(modernize-deprecated-headers)

// Linux with glibc specific: sigabbrev_np
auto get_signal_name(int sig) -> const char * {
    const char *name = sigabbrev_np(sig);
    if (name != nullptr) {
        return name;
    }
    return "Unknown signal";
}
auto get_error_name(int err) -> const char * {
    const char *name = strerrorname_np(err);
    if (name != nullptr) {
        return name;
    }
    return "Unknown error";
}
#elif defined(__APPLE__) || defined(__FreeBSD__)
// macOS and FreeBSD have sys_siglist
auto get_signal_name(int sig) -> const char * {
    if (sig >= 0 && sig < NSIG) {
        return sys_siglist[sig];
    }
    return "Unknown signal";
}
auto get_error_name(int err) -> const char * {
    if (err >= 0 && err < sys_nerr) {
        return sys_errlist[err];
    }
    return "Unknown error";
}
#else
auto get_signal_name(int sig) -> const char * { return strsignal(sig); }
auto get_error_name(int err) -> const char * { return strerror(err); }
#endif
