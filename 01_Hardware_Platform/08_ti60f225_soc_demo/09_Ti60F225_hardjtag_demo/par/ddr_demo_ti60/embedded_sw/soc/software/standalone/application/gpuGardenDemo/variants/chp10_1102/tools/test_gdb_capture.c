/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host-only native fixture: no serial, network, JTAG or target accesses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>

static void markers(const char *scenario)
{
    FILE *stream = strcmp(scenario, "stderr_markers") == 0 ? stderr : stdout;
    if (strcmp(scenario, "missing_load") != 0)
        fputs("Start address 0x00001000, load size 126944\n", stream);
    if (strcmp(scenario, "missing_restore") != 0)
        fputs("Restoring binary file atlas.bin into memory (0x5c4000 to 0x65e492)\n", stream);
    if (strcmp(scenario, "missing_relock") != 0)
        fputs("[HOST_RELOCK] verified=1 gpu=0x47505531 dbg=0x44424731 ctrl=0x4 q=0x400 mirror10=1\n", stream);
}

int main(int argc, char **argv)
{
    FILE *script;
    char scenario[80], cwd[4096];
    unsigned int i;
    if (argc != 4 || strcmp(argv[1], "-batch") != 0 || strcmp(argv[2], "-x") != 0)
        return 91;
    if (!_getcwd(cwd, sizeof(cwd)) || !strstr(cwd, "working directory"))
        return 92;
    script = fopen(argv[3], "rb");
    if (!script) return 93;
    if (!fgets(scenario, sizeof(scenario), script)) { fclose(script); return 94; }
    fclose(script);
    scenario[strcspn(scenario, "\r\n")] = '\0';
    if (strcmp(scenario, "empty") == 0) return 0;
    if (strcmp(scenario, "long_streams") == 0) {
        /* Interleave enough bytes to fill both pipe buffers. A reader which
           drains stdout fully before starting stderr will deadlock here. */
        for (i = 0; i < 512; ++i) {
            unsigned int j;
            for (j = 0; j < 1024; ++j) fputc('O', stdout);
            fputc('\n', stdout); fflush(stdout);
            for (j = 0; j < 1024; ++j) fputc('E', stderr);
            fputc('\n', stderr); fflush(stderr);
        }
    }
    fputs("warning: No executable has been specified and target does not support determining executable automatically.\n", stderr);
    if (strcmp(scenario, "warning_only") != 0) markers(scenario);
    if (strcmp(scenario, "error_exit") == 0) return 17;
    return 0;
}
