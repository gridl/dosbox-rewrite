
#ifdef HAVE_CONFIG_H
# include "config.h" // must be first
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>

#if HAVE_GCC_BUILTIN_CPU
# include <cpuid.h>
#endif

#include "dosboxxr/lib/hostcpudetect/caps.h"

// this is where this is defined
struct HostCPUCaps hostCPUcaps;

// C/C++ implementation of Perl's chomp function
void chomp(char *s) {
    char *t = s + strlen(s) - 1;
    while (t >= s && (*t == '\n' || *t == '\r')) *t-- = 0;
}

void HostCPUCaps::detect() {
#if defined(__i386__) || defined(__x86_64__)
# if HAVE_GCC_BUILTIN_CPU
/*------------------ x86/x86_64 we can use GCC builtins --------------*/
    __builtin_cpu_init();
    mmx = __builtin_cpu_supports("mmx");
    sse = __builtin_cpu_supports("sse");
    sse2 = __builtin_cpu_supports("sse2");
    sse3 = __builtin_cpu_supports("sse3");
    ssse3 = __builtin_cpu_supports("ssse3");
    avx = __builtin_cpu_supports("avx");
    avx2 = __builtin_cpu_supports("avx2");
# else//TODO: This block is Linux-specific
/*------------------ x86/x86_64 we can use procfs cpuinfo ------------*/
    char line[1024],*name,*value;
    int cpuno=0;
    FILE *fp;

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

