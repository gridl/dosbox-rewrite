
#ifdef HAVE_CONFIG_H
# include "config.h" // must be first
#endif

#if IS_WINDOWS
# include <windows.h>

// dammit MinGW
# ifndef PF_SSE3_INSTRUCTIONS_AVAILABLE
#  define PF_SSE3_INSTRUCTIONS_AVAILABLE 13
# endif
#endif

#include <sys/types.h>
#if IS_MAC_OSX
# include <sys/sysctl.h>
#endif
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#if HAVE_GCC_BUILTIN_CPU
# include <cpuid.h>
#endif

#include "dosboxxr/lib/text/chomp.h"
#include "dosboxxr/lib/hostcpudetect/caps.h"

// this is where this is defined
struct HostCPUCaps hostCPUcaps;

#if IS_WINDOWS
void HostCPUCaps::x86_win32_check_os_support(void) {
    /* When targeting Windows, we need to consider that MinGW/GCC uses CPUID to detect capabilities.
       The problem with x86 is that while CPUID can report SSE/AVX is available, it doesn't mean the
       OS has enabled it. Consider the following cases where we will crash if we blindly trust the
       CPU capability detection of GCC builtins:

       Windows XP on a CPU with AVX/AVX2 instructions (Windows XP does not support or enable AVX)
       Windows 98 and earlier on a CPU with SSE instructions (SSE support was not added until Windows 98 SE).

       The way to detect whether the OS has actually enabled it, is the IsProcessorFeaturePresent() API function.
       The OS will tell us whether SSE/AVX is actually available.

       Note that IsProcessorFeaturePresent() does not appear until Windows 98 SE and Windows 2000 (when SSE support appeared).
       That is the reason why we use GetProcAddress instead of assuming the symbol is present.
       If that symbol does not exist in KERNEL32.DLL, then we assume that anything other than MMX support does not exist in the OS. */
    BOOL (WINAPI *__IsProcessorFeaturePresent)(DWORD ProcessorFeature) =
        (BOOL (WINAPI *)(DWORD))GetProcAddress(GetModuleHandle("KERNEL32.DLL"),"IsProcessorFeaturePresent");

    if (__IsProcessorFeaturePresent != NULL) {
        // *SIGH* Guess what... Windows 98 SE always returns FALSE for PF_XMMI_INSTRUCTIONS_AVAILABLE.
        // So the best we can go on is to assume that if this entry point exists, that we're running under a version
        // of Windows that supports SSE even if the function does fuck all to help us.

        // Here's another one: IsProcessorFeaturePresent() cannot be used to detect AVX support either (added somewhere around Windows 7 SP1).
        // But, they added a KERNEL32.DLL API for it, and once again, we use that to detect OS support for AVX/AVX2 instructions.
        if (GetProcAddress(GetModuleHandle("KERNEL32.DLL"),"GetEnabledXStateFeatures") == NULL)
            avx = avx2 = false; // if it doesn't exist, then we can't use AVX and AVX2 instructions.
    }
    else {
        fprintf(stderr,"IsProcessorFeaturePresent doesn't exist\n");
        // IsProcessorFeaturePresent() doesn't exist. That means we're running on a version of Windows that pre-dates SSE extensions:
        //   Windows 95, 95 OSR2
        //   Windows 98 (first edition)
        //   Windows NT 4.x and earlier
        sse = sse2 = sse3 = ssse3 = false;
        avx = avx2 = false;
    }
}
#endif

void HostCPUCaps::detect() {
#if defined(__arm__)
# if IS_LINUX
/*------------------ arm we can use procfs cpuinfo ------------*/
    char line[1024],*name,*value;
    int cpuno=0;
    FILE *fp;

    detect_method = "Linux /proc/cpuinfo";

    if ((fp=fopen("/proc/cpuinfo","r")) != NULL) {
        memset(line,0,sizeof(line));
        while (!feof(fp) && !ferror(fp)) {
            if (fgets(line,sizeof(line)-1,fp) == NULL) break;
            chomp(line); // eat trailing newline

            /* empty lines separate one CPU from another */
            if (line[0] == 0) {
                cpuno++;
                continue;
            }

            /* "name" + \t + ": value" */
            name = value = line;
            value = strchr(line,':');
            if (value == NULL)
                continue;

            /* eat the tab/space chars prior to the colon */
            {
                char *x = value - 1;

                while (x >= name && (*x == '\t' || *x == ' ')) *x-- = 0;
            }

            *value++ = 0; /* ASCII overwrite : with NUL to split string */
            if (*value == ' ') value++;

            /* we're looking for features: ... */
            if (!strcasecmp(name,"features")) {
                /* each flag is separated by a space */
                while (*value != 0) {
                    if (*value == ' ') {
                        value++;
                        continue;
                    }

                    char *n = strchr(value,' ');
                    if (n != NULL) {
                        *n++ = 0; // only one space. overwrite with NUL to cut the string.
                    }
                    else {
                        n = value+strlen(value);
                    }

                    if (!strcmp(value,"neon"))
                        neon = 1;

                    // next flag
                    value = n;
                }
            }
        }
        fclose(fp);
    }
# endif
#elif defined(__i386__) || defined(__x86_64__)
# if HAVE_GCC_BUILTIN_CPU
/*------------------ x86/x86_64 we can use GCC builtins --------------*/
    detect_method = "GCC __builtin_cpu_init";

    __builtin_cpu_init();
    mmx = __builtin_cpu_supports("mmx");
    sse = __builtin_cpu_supports("sse");
    sse2 = __builtin_cpu_supports("sse2");
    sse3 = __builtin_cpu_supports("sse3");
    ssse3 = __builtin_cpu_supports("ssse3");
    avx = __builtin_cpu_supports("avx");
    avx2 = __builtin_cpu_supports("avx2");

#  if IS_WINDOWS
    x86_win32_check_os_support();
#  endif
# elif IS_MAC_OSX
/*------------------ x86/x86_64 Mac OS X has a way to query CPU features -------*/
    char line[4096],*value;
    size_t linelen = sizeof(line);

    detect_method = "Mac OS X (Darwin) sysctl";

    line[0] = 0;
    value = line;
    sysctlbyname("machdep.cpu.features",&line,&linelen,NULL,0);

    /* each flag is separated by a space */
    while (*value != 0) {
        if (*value == ' ') {
            value++;
            continue;
        }

        char *n = strchr(value,' ');
        if (n != NULL) {
            *n++ = 0; // only one space. overwrite with NUL to cut the string.
        }
        else {
            n = value+strlen(value);
        }

        if (!strcmp(value,"MMX"))
            mmx = 1;
        else if (!strcmp(value,"SSE"))
            sse = 1;
        else if (!strcmp(value,"SSE2"))
            sse2 = 1;
        else if (!strcmp(value,"SSE3"))
            sse3 = 1;
        else if (!strcmp(value,"SSSE3"))
            ssse3 = sse3 = 1; // FIXME, right?
        else if (!strcmp(value,"AVX1.0"))
            avx = 1;
        else if (!strcmp(value,"AVX2"))
            avx2 = 1;

        // next flag
        value = n;
    }

    line[0] = 0;
    value = line;
    sysctlbyname("machdep.cpu.leaf7_features",&line,&linelen,NULL,0);

    /* each flag is separated by a space */
    while (*value != 0) {
        if (*value == ' ') {
            value++;
            continue;
        }

        char *n = strchr(value,' ');
        if (n != NULL) {
            *n++ = 0; // only one space. overwrite with NUL to cut the string.
        }
        else {
            n = value+strlen(value);
        }

        if (!strcmp(value,"AVX2"))
            avx2 = 1;

        // next flag
        value = n;
    }
# elif IS_LINUX
/*------------------ x86/x86_64 we can use procfs cpuinfo ------------*/
    char line[1024],*name,*value;
    int cpuno=0;
    FILE *fp;

    detect_method = "Linux /proc/cpuinfo";

    if ((fp=fopen("/proc/cpuinfo","r")) != NULL) {
        memset(line,0,sizeof(line));
        while (!feof(fp) && !ferror(fp)) {
            if (fgets(line,sizeof(line)-1,fp) == NULL) break;
            chomp(line); // eat trailing newline

            /* empty lines separate one CPU from another */
            if (line[0] == 0) {
                cpuno++;
                continue;
            }

            /* "name" + \t + ": value" */
            name = value = line;
            value = strchr(line,':');
            if (value == NULL)
                continue;

            /* eat the tab/space chars prior to the colon */
            {
                char *x = value - 1;

                while (x >= name && (*x == '\t' || *x == ' ')) *x-- = 0;
            }

            *value++ = 0; /* ASCII overwrite : with NUL to split string */
            if (*value == ' ') value++;

            /* we're looking for flags: ... */
            if (!strcmp(name,"flags")) {
                /* each flag is separated by a space */
                while (*value != 0) {
                    if (*value == ' ') {
                        value++;
                        continue;
                    }

                    char *n = strchr(value,' ');
                    if (n != NULL) {
                        *n++ = 0; // only one space. overwrite with NUL to cut the string.
                    }
                    else {
                        n = value+strlen(value);
                    }

                    if (!strcmp(value,"mmx"))
                        mmx = 1;
                    else if (!strcmp(value,"sse"))
                        sse = 1;
                    else if (!strcmp(value,"sse2"))
                        sse2 = 1;
                    else if (!strcmp(value,"sse3"))
                        sse3 = 1;
                    else if (!strcmp(value,"ssse3"))
                        ssse3 = sse3 = 1; // FIXME, right?
                    else if (!strcmp(value,"avx"))
                        avx = 1;
                    else if (!strcmp(value,"avx2"))
                        avx2 = 1;

                    // next flag
                    value = n;
                }
            }
        }
        fclose(fp);
    }
# endif
#endif
}

