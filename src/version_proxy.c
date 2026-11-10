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
 *     triangle plus a particle 2dEffect, so the editor shows nothing. The editor shows their particles as
 *     the game plays them (models\effects.fxp, effectsPC.txd; fx.cpp), and a small coloured dot at the
 *     point of the effect: when it reads such a model from an IMG (GTAINTERFACE.dll IMGExportBuffer), it
 *     gets the dot model instead; the colour depends on the particle name. Game files are not touched.
 *  5. The world as SA-MP has it:
 *     - IPLs of the archives that are no streamed part of a text IPL are loaded only when SA-MP's main.scm
 *       asks for them (REQUEST_IPL): crack (the lab inside the San Fierro factory) yes; carter, truthsfarm,
 *       barriers1/2 no. The editor used to load them all.
 *     - SAMP\SAMP.ipl is loaded: SA-MP's gta.dat (from samp.saa) adds it after the game's IPLs.
 *     - The 74 objects SA-MP's main.scm makes as the game starts (cranes with their LODs, doors, portakabins,
 *       the searchlights of Area 51...) are loaded as one more IPL (scm_objects_ipl.h).
 *     - What that main.scm hides with SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE (the burnt shell of the San
 *       Fierro crack factory and its LOD) is hidden, and never exported as RemoveBuildingForPlayer.
 *  6. Closing one editor no longer terminates every editor.exe.
 *  7. "Point camera at these Coords" with only "x, y" keeps the camera height instead of failing.
 *  8. <INSERT> refuses an object ID that no loaded IDE defines (it used to break every redraw).
 *  9. The placed objects are backed up to backups\objects_<session start>.pwn next to the editor.
 *
 * If the bytes at any patch site differ from the expected ones (another editor build), nothing is patched.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fx.h"
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
#define BINIPL_LOAD       0x4E4888   /* loads it: [ebp-0C4h] holds its name                                */
#define AFTER_IPLS        0x4E494D   /* mov edx,'Processing LOD hierarchy'                               */
#define AFTER_IPLS_BACK   0x4E4952
#define STR_LOD_HIERARCHY 0x4E5428
#define FN_LOADIPL        0x4FA8AC   /* TMap.LoadIPL(Self, Kind, Path: AnsiString; Binary: Boolean)     */
#define STR_IPL           0x4E541C   /* 'IPL'                                                             */
#define MAP_PTR           0x578EF4   /* the map: +0x3C dynamic array of IPLs, each +8 array of instances */

static const BYTE BINIPL_ORIG[6]  = {0x0F,0x85,0xB9,0x00,0x00,0x00};
static const BYTE AFTER_ORIG[5]   = {0xBA,0x28,0x54,0x4E,0x00};

/* TForm1.FormClose (0x4E7DF4): after saving bookmarks/views it calls OpenProcess+TerminateProcess on an
   uninitialised PID, then KillTask(ExtractFileName(ExeName)) (0x4DCF10), which walks a Toolhelp snapshot
   and terminates every process named editor.exe - every other open editor too. Replaced by terminating
   only this process (the editor got there itself, when the walk reached it), after a last backup of
   the placed objects (editor_exit). */
#define CLOSE_KILL        0x4E7E22   /* mov eax,[ebp-0Ch]; push eax; push 0; push 401h; call OpenProcess  */

static const BYTE CLOSE_ORIG[11]  = {0x8B,0x45,0xF4, 0x50, 0x6A,0x00, 0x68,0x01,0x04,0x00,0x00};

/* TForm1.btn_camerahereClick ("Point camera at these Coords", 0x4E9948) splits the coordinates field and
   reads three numbers with GetFloat(i) (0x4999D4), which checks "Count >= i" instead of "Count > i": with
   only "x, y" it raises "List index out of bounds (2)" after the camera X/Y were written, leaving the
   camera half-moved. Its six GetFloat calls go through a stub: a missing number is the current look-at
   coordinate, so "x, y" keeps the camera's height. */
#define FN_GETFLOAT       0x4999D4   /* GetFloat(Index): Single, from the list SplitCoords filled           */
#define COORD_LIST        0x500C6C   /* that TStringList                                                   */
#define CAM_LOOKAT        0x578F04   /* camera look-at X, Y, Z (Single)                                    */

static const DWORD g_camerahere_calls[6] = {0x4E9982, 0x4E999B, 0x4E99B4, 0x4E99CA, 0x4E99DB, 0x4E99EC};

/* <INSERT> "Add object" (0x4EA8EC) takes any number from "Enter IDE number" and calls
   TForm1.insertobject(ID, x, y, z) (0x4EA594), which adds the instance to the map unchecked. With an ID
   that no IDE defines (111111) every redraw then fails with "Range check error", the message box comes
   back at once and the editor can only be killed. The call goes through a stub that refuses such IDs. */
#define ADDOBJ_CALL       0x4EAA78   /* call TForm1.insertobject; edx = ID, x/y/z pushed                  */
#define FN_INSERTOBJECT   0x4EA594   /* register (Self, ID) + 3 stack args, ret 0Ch                         */
#define IDE_BY_ID         0x57A5B8   /* array[0..19999] of the IDE definition, nil when not defined        */
#define IDE_MAX_ID        19999

/* Particles of the effect objects (fx.cpp): the view is taken right after the scene's gluLookAt (the
   5-byte "mov eax,[slot]" before "mov eax,[eax]; call eax" loads the address of fx_look_at_slot instead),
   the particles are drawn at the end of the scene drawing, before the selection is read */
#define LOOKAT_LOAD       0x4E1E94   /* mov eax,[4FF568h]; mov eax,[eax]; call eax  (gluLookAt)          */
#define SCENE_END         0x4E3D1C   /* cmp byte [4FF28Ch],1 (picking?) - 7 bytes, then jne / call 4DDCB4 */
#define SCENE_END_BACK    0x4E3D23

static const BYTE LOOKAT_ORIG[9]    = {0xA1,0x68,0xF5,0x4F,0x00, 0x8B,0x00, 0xFF,0xD0};
static const BYTE SCENE_END_ORIG[7] = {0x80,0x3D,0x8C,0xF2,0x4F,0x00,0x01};


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
    if (memcmp(VA(CLOSE_KILL), CLOSE_ORIG, sizeof CLOSE_ORIG)) return FALSE;
    for (i = 0; i < sizeof g_camerahere_calls / sizeof g_camerahere_calls[0]; i++) {
        BYTE *c = VA(g_camerahere_calls[i]);
        if (c[0] != 0xE8 || g_camerahere_calls[i] + 5 + *(DWORD *)(c + 1) != FN_GETFLOAT) return FALSE;
    }
    if (VA(ADDOBJ_CALL)[0] != 0xE8 || ADDOBJ_CALL + 5 + *(DWORD *)VA(ADDOBJ_CALL + 1) != FN_INSERTOBJECT)
        return FALSE;
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

typedef struct { BOOL has_geom; float max_radius; char fx[25]; float fx_pos[3]; } dff_scan;

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
                if (etype == 1 && dsz >= 24 && q + 20 + 24 <= p + size) {   /* particle: position, char name[24] */
                    memcpy(s->fx_pos, b + q, 12);
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

/* ---------- frames of simple objects, as the game has them ----------
 * The game loads a model of an objs/tobj IDE section as atomics only: every atomic is taken out of the
 * DFF's clump and given a new identity frame (CFileLoader::SetRelatedModelInfoCB), so the frame
 * transforms in the DFF do nothing. The editor applies them. Most DFFs have identity frames, but some
 * don't: lhouse_barrier1/2/3 (996/994/997) shift their geometry by 1.6-3.4 m along X, 0.5 m along Y and
 * -0.6..+0.2 m in height, each by a different amount, so barriers lined up in the editor stand apart in
 * the game. Each DFF the editor reads from an IMG gets identity frames, unless its model comes from
 * another section (anim, peds, cars, hier, weap), where the game keeps the hierarchy too. Those names are
 * noted as the editor's IDE loader reads them (hook at 0x4FC2D9, where the definition has its name). */

#define IDE_SECTION_HOOK  0x4FC2D9   /* mov eax,[ebp-18h] (section); mov edx,'objs' - 8 bytes            */
#define IDE_SECTION_BACK  0x4FC2E1
#define STR_OBJS          0x4FC52C

static const BYTE IDE_SECTION_ORIG[8] = {0x8B,0x45,0xE8, 0xBA,0x2C,0xC5,0x4F,0x00};

static BOOL g_frames_patched;        /* the IDE loader hook is in, so g_keep_frames is filled */
static char (*g_keep_frames)[24];    /* lower-case model names whose DFF keeps its frames */
static int g_keep_count, g_keep_cap;

static void lower_name(char *dst, const char *src, int n)
{
    int i;
    for (i = 0; i < n - 1 && src[i] && src[i] != '.'; i++)
        dst[i] = (char)((src[i] >= 'A' && src[i] <= 'Z') ? src[i] + 32 : src[i]);
    dst[i] = 0;
}

static BOOL keeps_frames(const char *dffname)
{
    char n[24];
    int i;
    lower_name(n, dffname, sizeof n);
    for (i = 0; i < g_keep_count; i++)
        if (!strcmp(g_keep_frames[i], n)) return TRUE;
    return FALSE;
}

/* called from the IDE loader for every definition: section and model name are Delphi strings */
static void __stdcall ide_definition(const char *section, const BYTE *def)
{
    const char *name = *(const char **)(def + 8);
    if (!section || !name) return;
    if (!lstrcmpiA(section, "objs") || !lstrcmpiA(section, "tobj")) return;
    if (keeps_frames(name)) return;
    if (g_keep_count == g_keep_cap) {
        int cap = g_keep_cap ? g_keep_cap * 2 : 256;
        void *p = realloc(g_keep_frames, cap * sizeof *g_keep_frames);
        if (!p) return;
        g_keep_frames = (char (*)[24])p;
        g_keep_cap = cap;
    }
    lower_name(g_keep_frames[g_keep_count++], name, 24);
}

static BYTE *build_ide_section_stub(void)
{
    BYTE *mem = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    emitter e;
    if (!mem) return NULL;
    e.p = e.base = mem;
    e_bytes(&e, "\x60", 1);                               /* pushad                       */
    e_bytes(&e, "\xFF\x75\xE0", 3);                       /* push [ebp-20h]  (definition) */
    e_bytes(&e, "\xFF\x75\xE8", 3);                       /* push [ebp-18h]  (section)    */
    e_rel(&e, 0xE8, (DWORD)ide_definition);
    e_bytes(&e, "\x61", 1);                               /* popad                        */
    e_bytes(&e, IDE_SECTION_ORIG, sizeof IDE_SECTION_ORIG);/* mov eax,[ebp-18h]; mov edx,'objs' */
    e_rel(&e, 0xE9, IDE_SECTION_BACK);
    FlushInstructionCache(GetCurrentProcess(), mem, e.p - mem);
    return mem;
}

/* every frame of the clump's frame list: identity rotation, zero position (parents stay) */
static void reset_frames(BYTE *b, DWORD size)
{
    DWORD pos = 12, end = 12 + rd32(b + 4);
    if (end > size) return;
    while (pos + 12 <= end) {
        DWORD type = rd32(b + pos), csize = rd32(b + pos + 4), p = pos + 12;
        if (csize > end - p) return;
        if (type == 0x0E && csize >= 16 && rd32(b + p) == 1) {   /* Frame List: Struct {count, frames} */
            DWORD n = rd32(b + p + 12), i;
            static const float ident[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};
            if (16 + n * 56 > csize) return;
            for (i = 0; i < n; i++) memcpy(b + p + 16 + i * 56, ident, sizeof ident);
            return;
        }
        pos = p + csize;
    }
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
    if (s.fx[0] && s.has_geom && s.max_radius < FX_MAX_RADIUS) {
        write_marker((BYTE *)buf, fx_colour(s.fx));
        fx_note_model((const char *)rec + 8, s.fx, s.fx_pos);   /* its particles are drawn by fx.cpp */
    }
    else if (g_frames_patched && !keeps_frames((const char *)rec + 8))
        reset_frames((BYTE *)buf, size);
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

/* The IPLs SA-MP's main.scm (R3) asks for as it starts: REQUEST_IPL 'CRACK' at 0xC3D5 of the script */
static int __cdecl requested_ipl(const char *name)
{
    return name && lstrcmpiA(name, "crack.ipl") == 0;
}

static int g_requested;

/* In the loop over the archives' IPLs not loaded yet, at "jne <next>" (ZF from inc eax: eax = IndexOf + 1):
   one already loaded goes on to the next as before, one that isn't is loaded only if the script asked */
static BYTE *build_binipl_stub(void)
{
    BYTE *mem = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    emitter e;
    if (!mem) return NULL;
    e.p = e.base = mem;
    *e.p++ = 0x0F; e_rel(&e, 0x85, BINIPL_NEXT);          /* jne next (loaded already)    */
    *e.p++ = 0x60;                                        /* pushad                       */
    e_bytes(&e, "\xFF\xB5\x3C\xFF\xFF\xFF", 6);       /* push dword [ebp-0C4h] (name) */
    e_rel(&e, 0xE8, (DWORD)requested_ipl);
    e_bytes(&e, "\x83\xC4\x04", 3);                      /* add esp,4                    */
    *e.p++ = 0xA3; e_u32(&e, (DWORD)&g_requested);        /* mov [g_requested],eax        */
    *e.p++ = 0x61;                                        /* popad                        */
    e_bytes(&e, "\x83\x3D", 2); e_u32(&e, (DWORD)&g_requested); *e.p++ = 0;   /* cmp [g_requested],0 */
    *e.p++ = 0x0F; e_rel(&e, 0x84, BINIPL_NEXT);          /* je next (not asked for)      */
    e_rel(&e, 0xE9, BINIPL_LOAD);                         /* load it                      */
    FlushInstructionCache(GetCurrentProcess(), mem, e.p - mem);
    return mem;
}

/* At the end of the scene drawing: the particles, then the instruction it replaced */
static BYTE *build_scene_end_stub(void)
{
    BYTE *mem = (BYTE *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    emitter e;
    if (!mem) return NULL;
    e.p = e.base = mem;
    *e.p++ = 0x60;                                        /* pushad                       */
    e_rel(&e, 0xE8, (DWORD)fx_after_scene);               /* call fx_after_scene          */
    *e.p++ = 0x61;                                        /* popad                        */
    e_bytes(&e, SCENE_END_ORIG, sizeof SCENE_END_ORIG);   /* cmp byte [picking],1         */
    e_rel(&e, 0xE9, SCENE_END_BACK);
    FlushInstructionCache(GetCurrentProcess(), mem, e.p - mem);
    return mem;
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

/* GetFloat(eax = index) for btn_camerahereClick: the editor's GetFloat when the list has that number,
   otherwise the current look-at coordinate with that index. Returns in st(0) like the original. */
static BYTE *build_getfloat_stub(void)
{
    BYTE *mem = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    emitter e;
    BYTE *je_at, *jle_at;
    if (!mem) return NULL;
    e.p = e.base = mem;

    e_bytes(&e, "\x50", 1);                               /* push eax (index)             */
    e_bytes(&e, "\x8B\x0D", 2); e_u32(&e, COORD_LIST);    /* mov ecx,[list]               */
    e_bytes(&e, "\x85\xC9", 2);                           /* test ecx,ecx                 */
    je_at = e.p; e_bytes(&e, "\x74\x00", 2);              /* je no_list                   */
    e_bytes(&e, "\x8B\xC1", 2);                           /* mov eax,ecx                  */
    e_bytes(&e, "\x8B\x10", 2);                           /* mov edx,[eax]                */
    e_bytes(&e, "\xFF\x52\x14", 3);                       /* call [edx+14h]  (GetCount)   */
    e_bytes(&e, "\x5A", 1);                               /* pop edx                      */
    e_bytes(&e, "\x3B\xC2", 2);                           /* cmp eax,edx                  */
    jle_at = e.p; e_bytes(&e, "\x7E\x00", 2);             /* jle missing                  */
    e_bytes(&e, "\x8B\xC2", 2);                           /* mov eax,edx                  */
    e_rel(&e, 0xE9, FN_GETFLOAT);                         /* the editor's GetFloat        */

    je_at[1] = (BYTE)(e.p - (je_at + 2));                 /* no_list:                     */
    e_bytes(&e, "\x5A", 1);                               /* pop edx                      */
    jle_at[1] = (BYTE)(e.p - (jle_at + 2));               /* missing:                     */
    e_bytes(&e, "\xD9\x04\x95", 3); e_u32(&e, CAM_LOOKAT);/* fld dword [CamLookAt+edx*4]  */
    e_bytes(&e, "\xC3", 1);                               /* ret                          */

    FlushInstructionCache(GetCurrentProcess(), mem, e.p - mem);
    return mem;
}

static BOOL __stdcall insert_id_ok(int id)
{
    char msg[160];
    if (id >= 0 && id <= IDE_MAX_ID && ((void **)IDE_BY_ID)[id]) return TRUE;
    wsprintfA(msg, "There is no object with ID %d in the loaded IDE files.", id);
    MessageBoxA(GetActiveWindow(), msg, "Add object", MB_OK | MB_ICONWARNING);
    return FALSE;
}

/* In place of "call TForm1.insertobject" in the <INSERT> handler: inserts only an ID some IDE defines */
static BYTE *build_addobj_stub(void)
{
    BYTE *mem = (BYTE *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    emitter e;
    if (!mem) return NULL;
    e.p = e.base = mem;

    e_bytes(&e, "\x52\x50\x52", 3);                       /* push edx; push eax; push edx */
    e_rel(&e, 0xE8, (DWORD)insert_id_ok);                 /* call insert_id_ok(ID)        */
    e_bytes(&e, "\x85\xC0", 2);                           /* test eax,eax                 */
    e_bytes(&e, "\x58\x5A", 2);                           /* pop eax; pop edx             */
    e_bytes(&e, "\x74\x05", 2);                           /* jz refuse                    */
    e_rel(&e, 0xE9, FN_INSERTOBJECT);                     /* insertobject(Self, ID, x,y,z)*/
    e_bytes(&e, "\xC2\x0C\x00", 3);                       /* refuse: ret 0Ch (drop x,y,z) */

    FlushInstructionCache(GetCurrentProcess(), mem, e.p - mem);
    return mem;
}

/* ---------- backup of the placed objects ----------
 * Every BACKUP_PERIOD ms (a thread timer of the editor's main thread, so the map is never read while the
 * editor changes it) the objects the editor would export - added objects and removed buildings, the same
 * test and lines as btn_exportmapClick (0x4E9B50) - are written to backups\objects_<session start>.pwn
 * next to the editor, when they changed. One file per session: a new start never overwrites what a crashed
 * one left. The file is written to .tmp and moved over, so a crash while writing keeps the previous one.
 * The editor's import reads the CreateObject / RemoveBuildingForPlayer lines back. */

#define BACKUP_PERIOD     2000
#define INST_ADDED        0x3A       /* byte: 1 = added in the editor                                      */
#define INST_REMOVED      0x3B       /* byte: 1 = removed (exported as RemoveBuildingForPlayer)            */

typedef struct { char *p; size_t len, cap; } textbuf;

static void tb_printf(textbuf *t, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (t->cap - t->len < 512) {
        size_t cap = t->cap ? t->cap * 2 : 65536;
        char *p = (char *)realloc(t->p, cap);
        if (!p) return;
        t->p = p; t->cap = cap;
    }
    va_start(ap, fmt);
    n = _vsnprintf_s(t->p + t->len, t->cap - t->len, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n > 0) t->len += n;
}

static int dynlen(const void *arr) { return arr ? ((const int *)arr)[-1] : 0; }

/* the lines of btn_exportmapClick (absolute coordinates), or FALSE when the map is not readable */
static BOOL collect_objects(textbuf *t, int *count)
{
    __try {
        BYTE *map = *(BYTE **)MAP_PTR;
        BYTE **ipls;
        int i, j;
        *count = 0;
        if (!map) return TRUE;
        ipls = *(BYTE ***)(map + 0x3C);
        for (i = 0; i < dynlen(ipls); i++) {
            BYTE **insts = *(BYTE ***)(ipls[i] + 8);
            for (j = 0; j < dynlen(insts); j++) {
                BYTE *o = insts[j];
                int id = *(int *)(o + 4);
                float *pos = (float *)(o + 0x18), *rot = (float *)(o + 0x40);
                if (o[INST_REMOVED] == 0 && o[INST_ADDED] == 1) {
                    if (id >= 400 && id <= 611)
                        tb_printf(t, "CreateVehicle(%d, %0.4f, %0.4f, %0.4f, %0.4f, -1, -1, 100);\r\n",
                                  id, pos[0], pos[1], pos[2], rot[2]);
                    else
                        tb_printf(t, "CreateObject(%d, %0.5f, %0.5f, %0.5f,   %0.5f, %0.5f, %0.5f);\r\n",
                                  id, pos[0], pos[1], pos[2], rot[0], rot[1], rot[2]);
                    ++*count;
                } else if (o[INST_REMOVED] == 1 && o[INST_ADDED] == 0) {
                    tb_printf(t, "RemoveBuildingForPlayer(playerid, %d, %0.4f, %0.4f, %0.4f, 0.25);\r\n",
                              id, pos[0], pos[1], pos[2]);
                    ++*count;
                }
            }
        }
        return TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

static char g_backup_path[MAX_PATH];
static char g_session_time[32];
static unsigned __int64 g_backup_hash;
static BOOL g_backup_written;

static void CALLBACK backup_tick(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    textbuf body = {0}, file = {0};
    unsigned __int64 h = 14695981039346656037ULL;
    int count;
    size_t i;
    (void)hwnd; (void)msg; (void)id; (void)time;

    if (!collect_objects(&body, &count)) goto done;
    for (i = 0; i < body.len; i++) { h ^= (BYTE)body.p[i]; h *= 1099511628211ULL; }
#ifndef BACKUP_TEST
    if (body.len == 0 && !g_backup_written) goto done;     /* nothing placed yet: no file */
#endif
    if (g_backup_written && h == g_backup_hash) goto done;

    {
        SYSTEMTIME st;
        char tmp[MAX_PATH + 4];
        HANDLE f;
        DWORD wr;
        BOOL ok;
        GetLocalTime(&st);
        tb_printf(&file, "// SA-MP Map Construction: backup of the placed objects (version.dll)\r\n"
                         "// session started %s, saved %04d-%02d-%02d %02d:%02d:%02d, %d lines\r\n\r\n",
                  g_session_time, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, count);
        if (!file.p) goto done;
        wsprintfA(tmp, "%s.tmp", g_backup_path);
        f = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) goto done;
        ok = WriteFile(f, file.p, (DWORD)file.len, &wr, NULL) && wr == file.len;
        if (ok && body.len) ok = WriteFile(f, body.p, (DWORD)body.len, &wr, NULL) && wr == body.len;
        ok = FlushFileBuffers(f) && ok;
        CloseHandle(f);
        if (ok && MoveFileExA(tmp, g_backup_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            g_backup_hash = h;
            g_backup_written = TRUE;
        } else {
            DeleteFileA(tmp);
        }
    }
done:
    free(body.p);
    free(file.p);
}

static void start_backup(void)
{
    char dir[MAX_PATH];
    char *slash;
    SYSTEMTIME st;
    DWORD n = GetModuleFileNameA(g_self, dir, MAX_PATH);
    if (!n || n >= MAX_PATH || !(slash = strrchr(dir, '\\'))) return;
    lstrcpyA(slash, "\\backups");
    CreateDirectoryA(dir, NULL);
    GetLocalTime(&st);
    wsprintfA(g_session_time, "%04d-%02d-%02d %02d:%02d:%02d",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (lstrlenA(dir) + 48 >= MAX_PATH) return;
    wsprintfA(g_backup_path, "%s\\objects_%04d-%02d-%02d_%02d-%02d-%02d_%lu.pwn", dir,
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    SetTimer(NULL, 0, BACKUP_PERIOD, backup_tick);
}

/* replaces the kill at the end of FormClose: the last state goes to the backup, then this editor ends */
static void editor_exit(void)
{
    if (g_backup_path[0]) backup_tick(NULL, 0, 0, 0);
    TerminateProcess(GetCurrentProcess(), 0);
}

/* ---------- About window: this project, its author and links ----------
 * Twnd_about is built from the DFM resource TWND_ABOUT. Below its text (alTop labels up to y=78) there is
 * room until the bottom panel (y=145) for four more 13 px lines, so the window keeps its size. A copy of
 * the DFM with four more TLabels goes into a buffer of this DLL. Windows refuses a resource whose data
 * lies outside the image ("Resource Twnd_about not found"), so the resource stays as it is and the
 * editor's imports of LoadResource / LockResource / SizeofResource hand out the copy for it. The two links
 * are clickable: their OnClick names two methods added to the form's published method table (a copy with
 * btn_clearClick plus them, the VMT points to it), which open the browser. */

#define ABOUT_DATA_ENTRY  0x5A1CA0   /* IMAGE_RESOURCE_DATA_ENTRY of RCDATA TWND_ABOUT                     */
#define ABOUT_DFM_RVA     0x1C9D94
#define ABOUT_DFM_SIZE    22585
#define ABOUT_INSERT_AT   0x4CB6     /* in the DFM: start of the Label6 object, after Image1               */
#define ABOUT_VMT_METHODS 0x4D9594   /* Twnd_about VMT: vmtMethodTable                                     */
#define ABOUT_METHODS     0x4D9747   /* its table: 1 entry, btn_clearClick                                  */
#define ABOUT_METHODS_LEN 23

#define URL_FORUM         "https://pawn.wiki/index.php?showtopic=595"
#define URL_REPO          "https://github.com/Romz24/mapconstruction-fix"

static const BYTE ABOUT_METHODS_ORIG[ABOUT_METHODS_LEN] = {
    0x01,0x00, 0x15,0x00, 0xA0,0x97,0x4D,0x00, 0x0E,'b','t','n','_','c','l','e','a','r','C','l','i','c','k'};

/* Delphi methods called as Self.Method(Sender) in eax/edx: no stack arguments, so plain cdecl fits */
static void open_url(const char *url)
{
    ShellExecuteA(GetActiveWindow(), "open", url, NULL, NULL, SW_SHOWNORMAL);
}
static void about_forum_click(void) { open_url(URL_FORUM); }
static void about_repo_click(void)  { open_url(URL_REPO); }

typedef struct { BYTE *p; } dfmw;
static void dw_bytes(dfmw *w, const void *b, size_t n) { memcpy(w->p, b, n); w->p += n; }
static void dw_ss(dfmw *w, const char *s) { size_t n = strlen(s); *w->p++ = (BYTE)n; dw_bytes(w, s, n); }
static void dw_int(dfmw *w, const char *prop, int v)
{
    short s = (short)v;
    dw_ss(w, prop); *w->p++ = 3; dw_bytes(w, &s, 2);          /* vaInt16 */
}
static void dw_ident(dfmw *w, const char *prop, const char *v) { dw_ss(w, prop); *w->p++ = 7; dw_ss(w, v); }
static void dw_str(dfmw *w, const char *prop, const char *v)   { dw_ss(w, prop); *w->p++ = 6; dw_ss(w, v); }

/* a centred alTop TLabel like the form's own; link: blue, underlined, hand cursor, OnClick */
static void dw_label(dfmw *w, const char *name, int top, const char *caption, const char *onclick)
{
    dw_ss(w, "TLabel"); dw_ss(w, name);
    dw_int(w, "Left", 0); dw_int(w, "Top", top); dw_int(w, "Width", 639); dw_int(w, "Height", 13);
    if (onclick) dw_ident(w, "Cursor", "crHandPoint");
    dw_ident(w, "Align", "alTop");
    dw_ident(w, "Alignment", "taCenter");
    dw_str(w, "Caption", caption);
    if (onclick) {
        dw_ident(w, "Font.Charset", "DEFAULT_CHARSET");
        dw_ident(w, "Font.Color", "clBlue");
        dw_int(w, "Font.Height", -11);
        dw_str(w, "Font.Name", "MS Sans Serif");
        dw_ss(w, "Font.Style"); *w->p++ = 11; dw_ss(w, "fsUnderline"); *w->p++ = 0;   /* vaSet */
        dw_ss(w, "ParentFont"); *w->p++ = 8;                                          /* vaFalse */
    }
    dw_ident(w, "Layout", "tlCenter");
    if (onclick) dw_ident(w, "OnClick", onclick);
    *w->p++ = 0;                                              /* end of properties */
    *w->p++ = 0;                                              /* no children */
}

static BYTE g_about_dfm[ABOUT_DFM_SIZE + 2048];
static DWORD g_about_dfm_len;
static BYTE g_about_methods[128];

static HGLOBAL (WINAPI *real_LoadResource)(HMODULE, HRSRC);
static LPVOID  (WINAPI *real_LockResource)(HGLOBAL);
static DWORD   (WINAPI *real_SizeofResource)(HMODULE, HRSRC);

static HGLOBAL WINAPI my_LoadResource(HMODULE mod, HRSRC res)
{
    if (res == (HRSRC)ABOUT_DATA_ENTRY) return (HGLOBAL)g_about_dfm;
    return real_LoadResource(mod, res);
}
static LPVOID WINAPI my_LockResource(HGLOBAL h)
{
    if (h == (HGLOBAL)g_about_dfm) return g_about_dfm;
    return real_LockResource(h);
}
static DWORD WINAPI my_SizeofResource(HMODULE mod, HRSRC res)
{
    if (res == (HRSRC)ABOUT_DATA_ENTRY) return g_about_dfm_len;
    return real_SizeofResource(mod, res);
}

/* every import slot of editor.exe that holds `real` gets `hook` */
static void hook_imports(void *real, void *hook)
{
    BYTE *base = (BYTE *)0x400000;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);
    for (; d->Name; d++) {
        DWORD *slot = (DWORD *)(base + d->FirstThunk);
        for (; *slot; slot++)
            if (*slot == (DWORD)real) write_mem(slot, &hook, 4);
    }
}

static BOOL about_matches(void)
{
    IMAGE_RESOURCE_DATA_ENTRY *de = (IMAGE_RESOURCE_DATA_ENTRY *)ABOUT_DATA_ENTRY;
    const BYTE *dfm = VA(0x400000 + ABOUT_DFM_RVA);
    return de->OffsetToData == ABOUT_DFM_RVA && de->Size == ABOUT_DFM_SIZE &&
           !memcmp(dfm, "TPF0\x0ATwnd_about", 15) &&
           !memcmp(dfm + ABOUT_INSERT_AT, "\x06TLabel\x06Label6", 14) &&
           *(DWORD *)ABOUT_VMT_METHODS == ABOUT_METHODS &&
           !memcmp(VA(ABOUT_METHODS), ABOUT_METHODS_ORIG, ABOUT_METHODS_LEN);
}

static void patch_about(void)
{
    const BYTE *dfm = VA(0x400000 + ABOUT_DFM_RVA);
    dfmw w;
    BYTE *m;
    DWORD tbl;

    w.p = g_about_dfm;
    dw_bytes(&w, dfm, ABOUT_INSERT_AT);
    dw_label(&w, "LabelFix0", 78, "", NULL);
    dw_label(&w, "LabelFix1", 91, "Fixes and improvements (mapconstruction-fix): Romzes", NULL);
    dw_label(&w, "LabelFix2", 104, URL_FORUM, "aboutForumClick");
    dw_label(&w, "LabelFix3", 117, URL_REPO, "aboutRepoClick");
    dw_bytes(&w, dfm + ABOUT_INSERT_AT, ABOUT_DFM_SIZE - ABOUT_INSERT_AT);

    /* method table: count, then {size, code, name} - btn_clearClick as it was, then ours */
    m = g_about_methods;
    *(WORD *)m = 3; m += 2;
    memcpy(m, ABOUT_METHODS_ORIG + 2, ABOUT_METHODS_LEN - 2); m += ABOUT_METHODS_LEN - 2;
    {
        static const char *names[2] = {"aboutForumClick", "aboutRepoClick"};
        void *codes[2];
        int k;
        codes[0] = (void *)about_forum_click;
        codes[1] = (void *)about_repo_click;
        for (k = 0; k < 2; k++) {
            BYTE n = (BYTE)strlen(names[k]);
            *(WORD *)m = (WORD)(2 + 4 + 1 + n);
            memcpy(m + 2, &codes[k], 4);
            m[6] = n;
            memcpy(m + 7, names[k], n);
            m += 7 + n;
        }
    }

    g_about_dfm_len = (DWORD)(w.p - g_about_dfm);
    {
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        real_LoadResource = (HGLOBAL (WINAPI *)(HMODULE, HRSRC))GetProcAddress(k32, "LoadResource");
        real_LockResource = (LPVOID (WINAPI *)(HGLOBAL))GetProcAddress(k32, "LockResource");
        real_SizeofResource = (DWORD (WINAPI *)(HMODULE, HRSRC))GetProcAddress(k32, "SizeofResource");
        if (!real_LoadResource || !real_LockResource || !real_SizeofResource) return;
    }
    tbl = (DWORD)g_about_methods;
    write_mem(VA(ABOUT_VMT_METHODS), &tbl, 4);
    hook_imports((void *)real_LoadResource, (void *)my_LoadResource);
    hook_imports((void *)real_LockResource, (void *)my_LockResource);
    hook_imports((void *)real_SizeofResource, (void *)my_SizeofResource);
}

/* ---------- Cyrillic (and any non-Western) text in the editor's controls ----------
 * The main form's DFM sets Font.Charset = ANSI_CHARSET (Western) and nearly every control inherits it,
 * so names typed in Russian (stored in the system code page, 1251) show as "Ïðèâåò". Graphics.Charsets, the
 * table that turns charset names from DFMs into numbers, gets ANSI_CHARSET = 1 (DEFAULT_CHARSET): fonts
 * then use the system code page, which is still Western on a Western system. */
#define CHARSET_ANSI_ENTRY 0x4FD6AC  /* Charsets[0]: Value 0, Name 'ANSI_CHARSET' (0x428248)              */
#define STR_ANSI_CHARSET   0x428248

static void patch_charset(void)
{
    DWORD *e = (DWORD *)CHARSET_ANSI_ENTRY;
    DWORD one = DEFAULT_CHARSET;
    if (e[0] == ANSI_CHARSET && e[1] == STR_ANSI_CHARSET && !memcmp((void *)STR_ANSI_CHARSET, "ANSI_CHARSET", 13))
        write_mem(e, &one, 4);
}

static void apply_patches(void)
{
    static const BYTE probe_new[2] = {0x75, 0x1A};        /* jne past the error + exit    */
    static const DWORD share_all[2] = {FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE};
    BYTE hook[13];
    BYTE *stub, *after, *binipl, *getfloat, *addobj;
    DWORD ed = (DWORD)&g_editor_dir;
    size_t i;

    if (!editor_matches()) return;
    stub = build_stub();
    after = build_after_ipls_stub();
    binipl = build_binipl_stub();
    getfloat = build_getfloat_stub();
    addobj = build_addobj_stub();
    if (!stub || !after || !binipl || !getfloat || !addobj) return;

    write_mem(VA(PROBE_JNE), probe_new, sizeof probe_new);
    write_mem(VA(SHARE_TABLE), share_all, sizeof share_all);
    for (i = 0; i < sizeof g_editor_refs / sizeof g_editor_refs[0]; i++)
        write_mem(VA(g_editor_refs[i]), &ed, 4);

    hook[0] = 0xE9;
    *(DWORD *)(hook + 1) = (DWORD)stub - (HOOK_SITE + 5);
    memset(hook + 5, 0x90, 8);
    write_mem(VA(HOOK_SITE), hook, sizeof hook);

    if (!memcmp(VA(IDE_SECTION_HOOK), IDE_SECTION_ORIG, sizeof IDE_SECTION_ORIG) &&
        *(DWORD *)VA(IDE_SECTION_HOOK + 4) == STR_OBJS) {
        BYTE *ide = build_ide_section_stub();
        if (ide) {
            BYTE jmp[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
            *(DWORD *)(jmp + 1) = (DWORD)ide - (IDE_SECTION_HOOK + 5);
            write_mem(VA(IDE_SECTION_HOOK), jmp, sizeof jmp);
            g_frames_patched = TRUE;
        }
    }

    {
        void *fn = (void *)hook_IMGExportBuffer;
        write_mem(VA(IAT_IMGEXPORTBUFFER), &fn, 4);
    }

    if (!memcmp(VA(LOOKAT_LOAD), LOOKAT_ORIG, sizeof LOOKAT_ORIG) &&
        !memcmp(VA(SCENE_END), SCENE_END_ORIG, sizeof SCENE_END_ORIG)) {
        BYTE *scene_end = build_scene_end_stub();
        if (scene_end) {
            BYTE load[5] = {0xB8};                        /* mov eax,&fx_look_at_slot     */
            BYTE jmp[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
            *(DWORD *)(load + 1) = (DWORD)&fx_look_at_slot;
            *(DWORD *)(jmp + 1) = (DWORD)scene_end - (SCENE_END + 5);
            write_mem(VA(LOOKAT_LOAD), load, sizeof load);
            write_mem(VA(SCENE_END), jmp, sizeof jmp);
        }
    }

    {
        BYTE skip[6] = {0xE9, 0, 0, 0, 0, 0x90};          /* jmp to the check of what the script asked */
        BYTE jump[5] = {0xE9};
        *(DWORD *)(skip + 1) = (DWORD)binipl - (BINIPL_JNE + 5);
        write_mem(VA(BINIPL_JNE), skip, sizeof skip);
        *(DWORD *)(jump + 1) = (DWORD)after - (AFTER_IPLS + 5);
        write_mem(VA(AFTER_IPLS), jump, sizeof jump);
    }

    {
        BYTE kill[5] = {0xE8};                            /* call editor_exit (never returns) */
        *(DWORD *)(kill + 1) = (DWORD)editor_exit - (CLOSE_KILL + 5);
        write_mem(VA(CLOSE_KILL), kill, sizeof kill);
    }

    for (i = 0; i < sizeof g_camerahere_calls / sizeof g_camerahere_calls[0]; i++) {
        DWORD rel = (DWORD)getfloat - (g_camerahere_calls[i] + 5);
        write_mem(VA(g_camerahere_calls[i] + 1), &rel, 4);
    }

    {
        DWORD rel = (DWORD)addobj - (ADDOBJ_CALL + 5);
        write_mem(VA(ADDOBJ_CALL + 1), &rel, 4);
    }

    if (about_matches()) patch_about();
    patch_charset();
    start_backup();
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
