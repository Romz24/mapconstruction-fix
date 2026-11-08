/*
 * version.dll proxy for SA-MP Map Construction (editor.exe, 1 310 720 bytes, md5 bdde354101733b90ff2c3499dab2ddbc).
 *
 * Put this DLL next to the ORIGINAL editor.exe. Windows loads it instead of the system version.dll
 * before the editor starts; it patches the editor in memory and forwards the three version.dll
 * functions the editor imports to the real system DLL. Delete the DLL to get stock behaviour back.
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
 *
 * If the bytes at any patch site differ from the expected ones (another editor build), nothing is patched.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

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

static void apply_patches(void)
{
    static const BYTE probe_new[2] = {0x75, 0x1A};        /* jne past the error + exit    */
    static const DWORD share_all[2] = {FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE};
    BYTE hook[13];
    BYTE *stub;
    DWORD ed = (DWORD)&g_editor_dir;
    size_t i;

    if (!editor_matches()) return;
    stub = build_stub();
    if (!stub) return;

    write_mem(VA(PROBE_JNE), probe_new, sizeof probe_new);
    write_mem(VA(SHARE_TABLE), share_all, sizeof share_all);
    for (i = 0; i < sizeof g_editor_refs / sizeof g_editor_refs[0]; i++)
        write_mem(VA(g_editor_refs[i]), &ed, 4);

    hook[0] = 0xE9;
    *(DWORD *)(hook + 1) = (DWORD)stub - (HOOK_SITE + 5);
    memset(hook + 5, 0x90, 8);
    write_mem(VA(HOOK_SITE), hook, sizeof hook);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        apply_patches();
    }
    return TRUE;
}
