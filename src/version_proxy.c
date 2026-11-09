/*
 * version.dll proxy for SA-MP Map Construction (editor.exe, 1 310 720 bytes, md5 bdde354101733b90ff2c3499dab2ddbc).
 *
 * Put this DLL next to the ORIGINAL editor.exe. Windows loads it instead of the system version.dll
 * before the editor starts; it patches the editor in memory and forwards every version.dll
 * function to the real system DLL. Delete the DLL to get stock behaviour back.
 *
 * Fixes:
 *  1. "You cannot use the editor while game is running" - the editor probes models\gta3.img with an
 *     exclusive read/write CreateFile and quits if it fails. The failure branch is skipped.
 *  2. Delphi SysUtils.FileOpen share-mode table: fmShareCompat / fmShareExclusive mean "share nothing".
 *     Both become FILE_SHARE_READ|FILE_SHARE_WRITE, so loading works while GTA (or anything else)
 *     has the files open.
 *  3. Editor in its own folder: game dir = folder of editor.exe if it contains data\gta.dat,
 *     otherwise its parent folder. The editor's own files (gta_shapes\ cache, PrefabPics\) are
 *     built from the editor folder instead of the game folder.
 *  4. Visible SA-MP effect objects (fire, smoke, explosions, water...): their DFFs contain only a 1 mm
 *     triangle plus a particle 2dEffect, so the editor shows nothing. When the editor reads such a model
 *     from an IMG (GTAINTERFACE.dll IMGExportBuffer), it gets a coloured diamond marker instead; the
 *     colour depends on the particle name. Game files are not touched.
 *  5. The world as SA-MP has it:
 *     - IPLs of the archives that are no streamed part of a text IPL (crack, carter, truthsfarm, barriers1/2)
 *       are not loaded: the game loads them only when its story script asks (REQUEST_IPL), SA-MP never does.
 *     - SAMP\SAMP.ipl is loaded: SA-MP's gta.dat (from samp.saa) adds it after the game's IPLs.
 *     - The 74 objects SA-MP's main.scm makes as the game starts (cranes with their LODs, doors, portakabins,
 *       the searchlights of Area 51...) are loaded as one more IPL (scm_objects_ipl.h).
 *     - What that main.scm hides with SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE (the burnt shell of the San
 *       Fierro crack factory and its LOD) is hidden, and never exported as RemoveBuildingForPlayer.
 *
 * If the bytes at any patch site differ from the expected ones (another editor build), nothing is patched.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "marker_dff.h"
#include "scm_objects_ipl.h"

/* ---------- forwarding to the real version.dll ----------
 * Every export of the system version.dll must exist here: once this module is loaded as "version.dll",
 * every other DLL in the process (e.g. the OpenGL driver, which uses the W/Ex functions) binds to it.
 * A missing export makes the graphics driver fail to load and OpenGL falls back to "GDI Generic".
 * Each stub resolves its target on first call and jumps there with the caller's stack untouched.
 * VerLanguageNameA/W are forwarded straight to KERNEL32 in version.def, like the system DLL does. */

static HMODULE g_real;
static FARPROC g_targets[15];
static const char *const g_names[15] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerQueryValueA", "VerQueryValueW",
};

static DWORD WINAPI fail_stub(void) { SetLastError(ERROR_PROC_NOT_FOUND); return 0; }

static FARPROC WINAPI resolve(int i)
{
    if (!g_targets[i]) {
        FARPROC p = NULL;
        if (!g_real) {
            char path[MAX_PATH];
            UINT n = GetSystemDirectoryA(path, MAX_PATH);   /* SysWOW64 for a 32-bit process */
            if (n && n < MAX_PATH - 14) {
                memcpy(path + n, "\\version.dll", sizeof "\\version.dll");
                g_real = LoadLibraryA(path);
            }
        }
        if (g_real) p = GetProcAddress(g_real, g_names[i]);
        /* fail_stub returns 0 without cleaning arguments: only reached if the system DLL is broken */
        g_targets[i] = p ? p : (FARPROC)fail_stub;
    }
    return g_targets[i];
}

#define STUB(i, name) __declspec(naked) void proxy_##name(void) { __asm { push i } __asm { call resolve } __asm { jmp eax } }
STUB(0,  GetFileVersionInfoA)
STUB(1,  GetFileVersionInfoByHandle)
STUB(2,  GetFileVersionInfoExA)
STUB(3,  GetFileVersionInfoExW)
STUB(4,  GetFileVersionInfoSizeA)
STUB(5,  GetFileVersionInfoSizeExA)
STUB(6,  GetFileVersionInfoSizeExW)
STUB(7,  GetFileVersionInfoSizeW)
STUB(8,  GetFileVersionInfoW)
STUB(9,  VerFindFileA)
STUB(10, VerFindFileW)
STUB(11, VerInstallFileA)
STUB(12, VerInstallFileW)
STUB(13, VerQueryValueA)
STUB(14, VerQueryValueW)

/* ---------- editor addresses (image base 0x400000, no ASLR) ---------- */

#define VA(x) ((BYTE *)(x))

#define GAME_DIR          0x578DD4   /* global AnsiString: game folder */
#define PROBE_JNE         0x4E4380   /* jne <error message> after the gta3.img probe */
#define SHARE_TABLE       0x4FD14C   /* SysUtils.FileOpen ShareMode[] */
#define HOOK_SITE         0x4E562E   /* GameDir := ExtractFileDir(Application.ExeName) */
#define HOOK_BACK         0x4E563B
#define FN_LSTRASG        0x40425C   /* System.@LStrAsg(var dest; src) */
#define FN_LSTRCAT3       0x404514   /* System.@LStrCat3(var dest; s1, s2) */
#define FN_FILEEXISTS     0x409228   /* SysUtils.FileExists */
#define FN_EXTRACTFILEDIR 0x409444   /* SysUtils.ExtractFileDir */
#define STR_GTA_DAT       0x4E5148   /* '\data\gta.dat' */

/* instructions that read GameDir to build gta_shapes\ and PrefabPics\ paths (address of the imm32) */
static const DWORD g_editor_refs[] = {
    0x4E808E, 0x4E8864, 0x4EB3C8, 0x4EB403, 0x4EB45A, 0x4EB495,
    0x4ED57C, 0x4ED5D2, 0x4ED64A, 0x4ED6A0,
    0x4F3B25, 0x4F3B5C, 0x4F3DA3, 0x4F4393, 0x4F43B8, 0x4F4752, 0x4F490E,
};

static const BYTE HOOK_ORIG[13]   = {0x8B,0x55,0xC4, 0xB8,0xD4,0x8D,0x57,0x00, 0xE8,0x21,0xEC,0xF1,0xFF};
static const BYTE PROBE_ORIG[2]   = {0x75,0x0B};
static const BYTE SHARE_ORIG[20]  = {0,0,0,0, 0,0,0,0, 1,0,0,0, 2,0,0,0, 3,0,0,0};

static char *g_editor_dir;         /* Delphi AnsiString (nil = '') - the editor's own folder */
static HINSTANCE g_self;

/* The IPL loading of TForm1.LoadMap: the loop over the archives' IPLs not loaded yet (as a stream part of
   a text IPL), then "Processing LOD hierarchy" */
#define BINIPL_JNE        0x4E4882   /* jne <next> when the IPL was loaded already                       */
#define BINIPL_NEXT       0x4E4941
#define AFTER_IPLS        0x4E494D   /* mov edx,'Processing LOD hierarchy'                               */
#define AFTER_IPLS_BACK   0x4E4952
#define STR_LOD_HIERARCHY 0x4E5428
#define FN_LOADIPL        0x4FA8AC   /* TMap.LoadIPL(Self, Kind, Path: AnsiString; Binary: Boolean)     */
#define STR_IPL           0x4E541C   /* 'IPL'                                                             */
#define MAP_PTR           0x578EF4   /* the map: +0x3C dynamic array of IPLs, each +8 array of instances */

static const BYTE BINIPL_ORIG[6]  = {0x0F,0x85,0xB9,0x00,0x00,0x00};
static const BYTE AFTER_ORIG[5]   = {0xBA,0x28,0x54,0x4E,0x00};

/* ---------- helpers ---------- */

static BOOL write_mem(void *dst, const void *src, SIZE_T n)
{
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return FALSE;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return TRUE;
}

typedef struct { BYTE *p; BYTE *base; } emitter;
static void e_bytes(emitter *e, const void *b, int n) { memcpy(e->p, b, n); e->p += n; }
static void e_u32(emitter *e, DWORD v) { memcpy(e->p, &v, 4); e->p += 4; }
static void e_rel(emitter *e, BYTE op, DWORD target)   /* call/jmp rel32 */
{
    *e->p++ = op;
    e_u32(e, target - (DWORD)(e->p + 4));
}

static BOOL editor_matches(void)
{
    BYTE *exe = (BYTE *)GetModuleHandleA(NULL);
    DWORD gd = GAME_DIR;
    size_t i;
    if (exe != (BYTE *)0x400000) return FALSE;
    if (memcmp(VA(HOOK_SITE), HOOK_ORIG, sizeof HOOK_ORIG)) return FALSE;
    if (memcmp(VA(PROBE_JNE), PROBE_ORIG, sizeof PROBE_ORIG)) return FALSE;
    if (memcmp(VA(SHARE_TABLE), SHARE_ORIG, sizeof SHARE_ORIG)) return FALSE;
    if (memcmp(VA(BINIPL_JNE), BINIPL_ORIG, sizeof BINIPL_ORIG)) return FALSE;
    if (memcmp(VA(AFTER_IPLS), AFTER_ORIG, sizeof AFTER_ORIG)) return FALSE;
    for (i = 0; i < sizeof g_editor_refs / sizeof g_editor_refs[0]; i++)
        if (memcmp(VA(g_editor_refs[i]), &gd, 4)) return FALSE;
    return TRUE;
}

/* Replacement for "GameDir := ExtractFileDir(ExeName)"; runs inside the editor's FormCreate frame,
   where [ebp-0x3C] holds ExtractFileDir(ExeName) and [ebp-0x40] is a free string temp. */
static BYTE *build_stub(void)
{
    BYTE *mem = (BYTE *)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    emitter e;
    BYTE *jne_at;
    DWORD ed = (DWORD)&g_editor_dir;
    if (!mem) return NULL;
    e.p = e.base = mem;

    e_bytes(&e, "\x8B\x55\xC4", 3);                       /* mov edx,[ebp-3Ch]            */
    *e.p++ = 0xB8; e_u32(&e, ed);                         /* mov eax,&EditorDir           */
    e_rel(&e, 0xE8, FN_LSTRASG);                          /* EditorDir := exe folder      */

    e_bytes(&e, "\x8D\x45\xC0", 3);                       /* lea eax,[ebp-40h]            */
    e_bytes(&e, "\x8B\x15", 2); e_u32(&e, ed);            /* mov edx,[EditorDir]          */
    *e.p++ = 0xB9; e_u32(&e, STR_GTA_DAT);                /* mov ecx,'\data\gta.dat'      */
    e_rel(&e, 0xE8, FN_LSTRCAT3);                         /* tmp := EditorDir+'\data\gta.dat' */
    e_bytes(&e, "\x8B\x45\xC0", 3);                       /* mov eax,[ebp-40h]            */
    e_rel(&e, 0xE8, FN_FILEEXISTS);
    e_bytes(&e, "\x84\xC0", 2);                           /* test al,al                   */
    jne_at = e.p; e_bytes(&e, "\x75\x00", 2);             /* jne have_game                */

    e_bytes(&e, "\x8D\x55\xC4", 3);                       /* lea edx,[ebp-3Ch]            */
    *e.p++ = 0xA1; e_u32(&e, ed);                         /* mov eax,[EditorDir]          */
    e_rel(&e, 0xE8, FN_EXTRACTFILEDIR);                   /* [ebp-3Ch] := parent folder   */

    jne_at[1] = (BYTE)(e.p - (jne_at + 2));               /* have_game:                   */
    e_bytes(&e, "\x8B\x55\xC4", 3);                       /* mov edx,[ebp-3Ch]            */
    *e.p++ = 0xB8; e_u32(&e, GAME_DIR);                   /* mov eax,&GameDir             */
    e_rel(&e, 0xE8, FN_LSTRASG);                          /* GameDir := ...               */
    e_rel(&e, 0xE9, HOOK_BACK);                           /* back to the editor           */

    FlushInstructionCache(GetCurrentProcess(), mem, e.p - mem);
    return mem;
}

/* ---------- effect objects -> marker models ---------- */

#define IAT_IMGEXPORTBUFFER 0x58E994      /* editor.exe import slot for GTAINTERFACE.IMGExportBuffer */
#define FX_MAX_RADIUS       0.005f        /* effect DFFs have a 1 mm dummy triangle */

typedef void (WINAPI *IMGExportBuffer_t)(int index, void *buf);
typedef void (WINAPI *IMGGetThisFile_t)(void *rec, int index);   /* rec: {offset, sectors, name[24]} */

static IMGExportBuffer_t g_img_export;
static IMGGetThisFile_t  g_img_getfile;

static DWORD rd32(const BYTE *p) { DWORD v; memcpy(&v, p, 4); return v; }

typedef struct { BOOL has_geom; float max_radius; char fx[25]; } dff_scan;

/* Walks the RenderWare chunk tree in [pos, end): records geometry bounding radii and the first particle 2dEffect. */
static void scan_chunks(const BYTE *b, DWORD pos, DWORD end, dff_scan *s, int depth)
{
    while (depth < 8 && pos + 12 <= end) {
        DWORD type = rd32(b + pos), size = rd32(b + pos + 4), p = pos + 12;
        if (size > end - p) return;
        if (type == 0x0F && size >= 28) {                         /* Geometry: Struct header + fixed part */
            DWORD sp = p + 12, fmt = rd32(b + sp), nt = rd32(b + sp + 4), nv = rd32(b + sp + 8);
            DWORD flags = fmt & 0xFFFF, nuv = (fmt >> 16) & 0xFF, q;
            if (!nuv) nuv = (flags & 0x80) ? 2 : (flags & 0x04) ? 1 : 0;
            if (nv < 0x10000 && nt < 0x10000) {
                q = sp + 16 + ((flags & 0x08) ? 4 * nv : 0) + 8 * nv * nuv + 8 * nt;
                if (q + 16 <= p + size) {
                    float r; memcpy(&r, b + q + 12, 4);
                    if (!s->has_geom || r > s->max_radius) s->max_radius = r;
                    s->has_geom = TRUE;
                }
            }
        } else if (type == 0x253F2F8 && size >= 4 && !s->fx[0]) {  /* 2dEffect: count, then entries */
            DWORD n = rd32(b + p), q = p + 4, i;
            for (i = 0; i < n && q + 20 <= p + size; i++) {
                DWORD etype = rd32(b + q + 12), dsz = rd32(b + q + 16);
                if (etype == 1 && dsz >= 24 && q + 20 + 24 <= p + size) {   /* particle: char name[24] */
                    memcpy(s->fx, b + q + 20, 24); s->fx[24] = 0; break;
                }
                if (dsz > p + size - q - 20) break;
                q += 20 + dsz;
            }
        }
        if (type == 0x10 || type == 0x1A || type == 0x0F || type == 0x03 || type == 0x0E || type == 0x14)
            scan_chunks(b, p, p + size, s, depth + 1);
        pos = p + size;
    }
}

static BOOL has(const char *s, const char *sub) { return strstr(s, sub) != NULL; }

/* Marker colour (R,G,B) by particle name. */
static DWORD fx_colour(const char *name)
{
    char s[25]; int i;
    for (i = 0; i < 24 && name[i]; i++) s[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    s[i] = 0;
    if (has(s, "explosion") || has(s, "wallbust"))                                         return RGB(235, 40, 30);
    if (has(s, "fire") || has(s, "flame") || has(s, "molotov") || has(s, "jetthrust") ||
        has(s, "gunflash") || has(s, "camflash") || has(s, "shootlight"))                   return RGB(255, 130, 0);
    if (has(s, "water") || has(s, "splash") || has(s, "bubble") || has(s, "wake") || has(s, "swim") ||
        has(s, "boat") || has(s, "carwash") || has(s, "extinguisher"))                      return RGB(40, 150, 255);
    if (has(s, "spark") || has(s, "glass") || has(s, "debris") || has(s, "gunshell") || has(s, "nitro") ||
        has(s, "overheat") || has(s, "jetpack") || has(s, "petrol") || has(s, "spraycan"))  return RGB(255, 225, 0);
    if (has(s, "blood") || has(s, "puke") || has(s, "coke"))                               return RGB(210, 0, 130);
    if (has(s, "smoke") || has(s, "exhale") || has(s, "puff") || has(s, "cloud") || has(s, "teargas") ||
        has(s, "vent") || has(s, "dust") || has(s, "cement") || has(s, "sand") || has(s, "dirt") ||
        has(s, "insects") || has(s, "tree_hit"))                                            return RGB(160, 160, 160);
    return RGB(0, 220, 110);
}

static void write_marker(BYTE *buf, DWORD colour)
{
    BYTE r = GetRValue(colour), g = GetGValue(colour), bl = GetBValue(colour);
    BYTE *pre = buf + MARKER_PRELIT_OFFSET;
    int i;
    memcpy(buf, marker_dff, MARKER_DFF_SIZE);
    for (i = 0; i < MARKER_VERTS; i++) {                       /* prelit RGBA with baked face shading */
        pre[i * 4 + 0] = (BYTE)(r * marker_shade[i] / 255);
        pre[i * 4 + 1] = (BYTE)(g * marker_shade[i] / 255);
        pre[i * 4 + 2] = (BYTE)(bl * marker_shade[i] / 255);
        pre[i * 4 + 3] = 255;
    }
    buf[MARKER_MATCOL_OFFSET + 0] = r;                          /* material colour RGBA */
    buf[MARKER_MATCOL_OFFSET + 1] = g;
    buf[MARKER_MATCOL_OFFSET + 2] = bl;
    buf[MARKER_MATCOL_OFFSET + 3] = 255;
}

static void WINAPI hook_IMGExportBuffer(int index, void *buf)
{
    BYTE rec[32];
    DWORD size;
    dff_scan s;

    if (!g_img_export) {
        HMODULE gi = GetModuleHandleA("GTAINTERFACE.dll");
        if (gi) {
            g_img_export  = (IMGExportBuffer_t)GetProcAddress(gi, "IMGExportBuffer");
            g_img_getfile = (IMGGetThisFile_t)GetProcAddress(gi, "IMGGetThisFile");
        }
        if (!g_img_export) return;
    }
    g_img_export(index, buf);
    if (!g_img_getfile || !buf) return;

    g_img_getfile(rec, index);
    size = rd32(rec + 4) * 2048;                               /* the editor allocates exactly this much */
    if (size < MARKER_DFF_SIZE || size > 0x100000) return;
    if (rd32((BYTE *)buf) != 0x10) return;                      /* not a clump */

    memset(&s, 0, sizeof s);
    scan_chunks((const BYTE *)buf, 0, size, &s, 0);
    if (s.fx[0] && s.has_geom && s.max_radius < FX_MAX_RADIUS)
        write_marker((BYTE *)buf, fx_colour(s.fx));
}

/* ---------- the world as SA-MP has it ---------- */

/* An instance of an IPL as the editor keeps it */
#define INST_MODEL   0x04   /* int                                                                   */
#define INST_POS     0x18   /* float x, y, z                                                         */
#define INST_MADE    0x3A   /* byte: made in the editor (0 = of the map)                             */
#define INST_GONE    0x3B   /* byte: 0 shown; 1 removed (exported as RemoveBuildingForPlayer, "new
                               map" brings it back); drawing, picking and the IPL export skip any non-0 */
#define INST_HIDDEN  2      /* ours: hidden like a removed one, neither exported nor brought back    */

/* SA-MP's main.scm (R3), SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE at 0xC3DE-0xC42C (again at 0xC4A8): the
   crack factory of San Fierro whole, its burnt shell (CF_EXT_DEM_SFS) and the shell's LOD hidden */
static const struct { int model; float x, y, z, radius; BOOL visible; } g_script_visibility[] = {
    {11012, -2166.86f, -236.5f,  40.86f, 40.0f, TRUE},    /* CRACKFACT_SFS    */
    {11088, -2185.49f, -215.55f, 34.31f, 40.0f, FALSE},   /* CF_EXT_DEM_SFS   */
    {11270, -2166.86f, -236.5f,  40.86f, 40.0f, TRUE},    /* LODCRACKFACT_SFS */
    {11282, -2185.49f, -215.55f, 34.31f, 40.0f, FALSE},   /* LODEXT_DEM_SFS   */
};

static int dyn_len(void *arr) { return arr ? ((int *)arr)[-1] : 0; }   /* Delphi dynamic array */

/* The instance of the model closest to a point within a radius (3D), as the script command finds it */
static void set_visibility_of_closest(int model, float x, float y, float z, float radius, BOOL visible)
{
    BYTE *map = *(BYTE **)MAP_PTR, *best = NULL;
    BYTE **ipls;
    float min = radius * radius;
    int i, j;
    if (!map) return;
    ipls = *(BYTE ***)(map + 0x3C);
    for (i = 0; i < dyn_len(ipls); i++) {
        BYTE **inst = *(BYTE ***)(ipls[i] + 8);
        for (j = 0; j < dyn_len(inst); j++) {
            const float *p = (const float *)(inst[j] + INST_POS);
            float dx = p[0] - x, dy = p[1] - y, dz = p[2] - z, d = dx * dx + dy * dy + dz * dz;
            if (*(int *)(inst[j] + INST_MODEL) != model || d >= min) continue;
            min = d;
            best = inst[j];
        }
    }
    if (!best || best[INST_MADE]) return;
    if (!visible && !best[INST_GONE]) best[INST_GONE] = INST_HIDDEN;
    if (visible && best[INST_GONE] == INST_HIDDEN) best[INST_GONE] = 0;
}

/* A path as a Delphi AnsiString constant (reference count -1): the editor keeps it as it is */
typedef struct { LONG refs; LONG len; char s[MAX_PATH + 1]; } delphi_str;

static void load_ipl(delphi_str *path)
{
    DWORD fn = FN_LOADIPL, map = *(DWORD *)MAP_PTR;
    const char *chars = path->s;
    path->refs = -1;
    path->len = (LONG)strlen(path->s);
    __asm {
        push 0                  /* Binary := False: a text IPL from the disk */
        mov ecx, chars
        mov edx, STR_IPL
        mov eax, map
        call fn                 /* register convention, the callee takes its stack argument */
    }
}

/* After the editor's own IPLs, before it links the LODs: SA-MP's IPL, the script's objects, what it hides */
static void after_ipls(void)
{
    static delphi_str samp, scm;
    const char *game = *(const char **)GAME_DIR;
    char *slash;
    HANDLE f;
    DWORD written;
    size_t i;

    if (game) {
        lstrcpynA(samp.s, game, MAX_PATH - 16);
        lstrcatA(samp.s, "\\SAMP\\SAMP.ipl");
        if (GetFileAttributesA(samp.s) != INVALID_FILE_ATTRIBUTES) load_ipl(&samp);
    }

    /* Next to this DLL (the editor's folder). The editor skips IPLs whose path has "path" in it */
    GetModuleFileNameA(g_self, scm.s, MAX_PATH - 20);
    slash = strrchr(scm.s, '\\');
    if (slash) {
        lstrcpyA(slash + 1, "samp_scm.ipl");
        f = CreateFileA(scm.s, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            BOOL ok = WriteFile(f, scm_objects_ipl, sizeof scm_objects_ipl - 1, &written, NULL);
            CloseHandle(f);
            if (ok && written == sizeof scm_objects_ipl - 1) load_ipl(&scm);
        }
    }

    for (i = 0; i < sizeof g_script_visibility / sizeof g_script_visibility[0]; i++)
        set_visibility_of_closest(g_script_visibility[i].model, g_script_visibility[i].x, g_script_visibility[i].y,
                                  g_script_visibility[i].z, g_script_visibility[i].radius, g_script_visibility[i].visible);
}

/* At "mov edx,'Processing LOD hierarchy'": our IPLs, then the instruction it replaced */
static BYTE *build_after_ipls_stub(void)
{
    BYTE *mem = (BYTE *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    emitter e;
    if (!mem) return NULL;
    e.p = e.base = mem;
    *e.p++ = 0x60;                                        /* pushad                       */
    e_rel(&e, 0xE8, (DWORD)after_ipls);                   /* call after_ipls              */
    *e.p++ = 0x61;                                        /* popad                        */
    *e.p++ = 0xBA; e_u32(&e, STR_LOD_HIERARCHY);          /* mov edx,'Processing LOD...'  */
    e_rel(&e, 0xE9, AFTER_IPLS_BACK);
    FlushInstructionCache(GetCurrentProcess(), mem, e.p - mem);
    return mem;
}

static void apply_patches(void)
{
    static const BYTE probe_new[2] = {0x75, 0x1A};        /* jne past the error + exit    */
    static const DWORD share_all[2] = {FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE};
    BYTE hook[13];
    BYTE *stub, *after;
    DWORD ed = (DWORD)&g_editor_dir;
    size_t i;

    if (!editor_matches()) return;
    stub = build_stub();
    after = build_after_ipls_stub();
    if (!stub || !after) return;

    write_mem(VA(PROBE_JNE), probe_new, sizeof probe_new);
    write_mem(VA(SHARE_TABLE), share_all, sizeof share_all);
    for (i = 0; i < sizeof g_editor_refs / sizeof g_editor_refs[0]; i++)
        write_mem(VA(g_editor_refs[i]), &ed, 4);

    hook[0] = 0xE9;
    *(DWORD *)(hook + 1) = (DWORD)stub - (HOOK_SITE + 5);
    memset(hook + 5, 0x90, 8);
    write_mem(VA(HOOK_SITE), hook, sizeof hook);

    {
        void *fn = (void *)hook_IMGExportBuffer;
        write_mem(VA(IAT_IMGEXPORTBUFFER), &fn, 4);
    }

    {
        BYTE skip[6] = {0xE9, 0, 0, 0, 0, 0x90};          /* jmp <next>: no IPL is left over to load */
        BYTE jump[5] = {0xE9};
        *(DWORD *)(skip + 1) = BINIPL_NEXT - (BINIPL_JNE + 5);
        write_mem(VA(BINIPL_JNE), skip, sizeof skip);
        *(DWORD *)(jump + 1) = (DWORD)after - (AFTER_IPLS + 5);
        write_mem(VA(AFTER_IPLS), jump, sizeof jump);
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        g_self = inst;
        apply_patches();
    }
    return TRUE;
}
