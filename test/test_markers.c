/*
 * Offline test for the effect-marker hook: loads the real GTAINTERFACE.dll, opens IMG archives and runs
 * every DFF through hook_IMGExportBuffer exactly like the editor does.
 * Checks: effect-only models become the marker, every other file comes back byte-identical.
 *
 * Usage: test_markers.exe <folder with GTAINTERFACE.dll> <img> [<img> ...]
 */
#include "../src/version_proxy.c"
#include <stdio.h>
#include <stdlib.h>

typedef void (WINAPI *IMGLoadImg_t)(const char *path);
typedef int  (WINAPI *IMGFileCount_t)(void);
typedef const char *(WINAPI *IMGGetFileName_t)(int index);

int main(int argc, char **argv)
{
    char dll[MAX_PATH];
    HMODULE gi;
    IMGLoadImg_t load; IMGFileCount_t count; IMGGetFileName_t getname; IMGGetThisFile_t getfile; IMGExportBuffer_t exportbuf;
    int a, failures = 0;

    if (argc < 3) { printf("usage: %s <editor folder> <img>...\n", argv[0]); return 2; }
    snprintf(dll, sizeof dll, "%s\\GTAINTERFACE.dll", argv[1]);
    gi = LoadLibraryA(dll);
    if (!gi) { printf("cannot load %s (%lu)\n", dll, GetLastError()); return 2; }
    load = (IMGLoadImg_t)GetProcAddress(gi, "IMGLoadImg");
    count = (IMGFileCount_t)GetProcAddress(gi, "IMGFileCount");
    getname = (IMGGetFileName_t)GetProcAddress(gi, "IMGGetFileName");
    getfile = (IMGGetThisFile_t)GetProcAddress(gi, "IMGGetThisFile");
    exportbuf = (IMGExportBuffer_t)GetProcAddress(gi, "IMGExportBuffer");

    for (a = 2; a < argc; a++) {
        int i, n, dffs = 0, markers = 0, same = 0;
        load(argv[a]);
        n = count();
        for (i = 0; i < n; i++) {
            BYTE rec[32]; DWORD size; BYTE *orig, *hooked;
            char name[25];
            memcpy(name, getname(i), 24); name[24] = 0;
            if (!strstr(_strlwr(name), ".dff")) continue;
            getfile(rec, i); size = rd32(rec + 4) * 2048;
            if (!size) continue;
            orig = (BYTE *)malloc(size); hooked = (BYTE *)malloc(size);
            exportbuf(i, orig);
            hook_IMGExportBuffer(i, hooked);
            dffs++;
            if (!memcmp(hooked, orig, size)) same++;
            else if (!memcmp(hooked + 12, marker_dff + 12, MARKER_PRELIT_OFFSET - 12)) {
                markers++;
                printf("  marker  %-24s colour %02x%02x%02x\n", name,
                       hooked[MARKER_MATCOL_OFFSET], hooked[MARKER_MATCOL_OFFSET + 1], hooked[MARKER_MATCOL_OFFSET + 2]);
            } else { printf("  CHANGED %s but not a marker!\n", name); failures++; }
            free(orig); free(hooked);
        }
        printf("%s: %d dff, %d markers, %d unchanged\n", argv[a], dffs, markers, same);
        if (markers + same != dffs) failures++;
    }
    printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
