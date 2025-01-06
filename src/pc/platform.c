#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#endif

#include "cliopts.h"
#include "fs/fs.h"
#include "configfile.h"

/* NULL terminated list of platform specific read-only data paths */
/* priority is top first */
const char *sys_ropaths[] = {
    ".", // working directory
    "!", // executable directory
#if defined(__linux__) || defined(__unix__)
    // some common UNIX directories for read only stuff
    "/usr/local/share/sm64pc",
    "/usr/share/sm64pc",
    "/opt/sm64pc",
#endif
    NULL,
};

/* these are not available on some platforms, so might as well */

char *sys_strlwr(char *src) {
  for (unsigned char *p = (unsigned char *)src; *p; p++)
     *p = tolower(*p);
  return src;
}

char *sys_strdup(const char *src) {
    const unsigned len = strlen(src) + 1;
    char *newstr = malloc(len);
    if (newstr) memcpy(newstr, src, len);
    return newstr;
}

int sys_strcasecmp(const char *s1, const char *s2) {
    const unsigned char *p1 = (const unsigned char *) s1;
    const unsigned char *p2 = (const unsigned char *) s2;
    int result;
    if (p1 == p2)
        return 0;
    while ((result = tolower(*p1) - tolower(*p2++)) == 0)
        if (*p1++ == '\0')
            break;
    return result;
}

const char *sys_file_extension(const char *fpath) {
    const char *fname = sys_file_name(fpath);
    const char *dot = strrchr(fname, '.');
    if (!dot || !dot[1]) return NULL; // no dot
    if (dot == fname) return NULL; // dot is the first char (e.g. .local)
    return dot + 1;
}

const char *sys_file_name(const char *fpath) {
    const char *sep1 = strrchr(fpath, '/');
    const char *sep2 = strrchr(fpath, '\\');
    const char *sep = sep1 > sep2 ? sep1 : sep2;
    if (!sep) return fpath;
    return sep + 1;
}

void sys_swap_backslashes(char* buffer) {
    size_t length = strlen(buffer);
    bool inColor = false;
    for (u32 i = 0; i < length; i++) {
        if (buffer[i] == '\\' && buffer[MIN(i + 1, length)] == '#') { inColor = true; }
        if (buffer[i] == '\\' && !inColor) { buffer[i] = '/'; }
        if (buffer[i] == '\\' && inColor && buffer[MIN( i + 1, length)] != '#') { inColor = false; }
    }
}

/* this calls a platform-specific impl function after forming the error message */

static void sys_fatal_impl(const char *msg) __attribute__ ((noreturn));

#ifdef _WIN32

static bool sys_windows_pathname_is_portable(const wchar_t *name, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        wchar_t c = name[i];

        // character outside the ASCII printable range
        if ((c < L' ') || (c > L'~')) { return false; }

        // characters unallowed in filenames
        switch (c) {
            // skipping ':', as it will appear with the drive specifier
            case L'<': case L'>': case L'/': case L'\\':
            case L'"': case L'|': case L'?': case L'*':
                return false;
        }
    }
    return true;
}

static wchar_t *sys_windows_pathname_get_delim(const wchar_t *name)
{
    const wchar_t *sep1 = wcschr(name, L'/');
    const wchar_t *sep2 = wcschr(name, L'\\');

    if (NULL == sep1) { return (wchar_t*)sep2; }
    if (NULL == sep2) { return (wchar_t*)sep1; }

    return (sep1 < sep2) ? (wchar_t*)sep1 : (wchar_t*)sep2;
}

bool sys_windows_short_path_from_wcs(char *destPath, size_t destSize, const wchar_t *wcsLongPath)
{
    wchar_t wcsShortPath[SYS_MAX_PATH]; // converted with WinAPI
    wchar_t wcsPortablePath[SYS_MAX_PATH]; // non-unicode parts replaced back with long forms

    // Convert the Long Path in Wide Format to the alternate short form.
    // It will still point to already existing directory or file.
    if (0 == GetShortPathNameW(wcsLongPath, wcsShortPath, SYS_MAX_PATH)) { return FALSE; }

    // Scanning the paths side-by-side, to keep the portable (ASCII)
    // parts of the absolute path unchanged (in the long form)
    wcsPortablePath[0] = L'\0';
    const wchar_t *longPart = wcsLongPath;
    wchar_t *shortPart = wcsShortPath;

    while (true) {
        int longLength;
        int shortLength;
        const wchar_t *sourcePart;
        int sourceLength;
        int bufferLength;

        const wchar_t *longDelim = sys_windows_pathname_get_delim(longPart);
        wchar_t *shortDelim = sys_windows_pathname_get_delim(shortPart);

        if (NULL == longDelim) {
            longLength = wcslen(longPart); // final part of the scanned path
        } else {
            longLength = longDelim - longPart; // ptr diff measured in WCHARs
        }

        if (NULL == shortDelim) {
            shortLength = wcslen(shortPart); // final part of the scanned path
        } else {
            shortLength = shortDelim - shortPart; // ptr diff measured in WCHARs
        }

        if (sys_windows_pathname_is_portable(longPart, longLength)) {
            // take the original name (subdir or filename)
            sourcePart = longPart;
            sourceLength = longLength;
        } else {
            // take the converted alternate (short) name
            sourcePart = shortPart;
            sourceLength = shortLength;
        }

        // take into account the slash-or-backslash separator
        if (L'\0' != sourcePart[sourceLength]) { sourceLength++; }

        // how many WCHARs are still left in the buffer
        bufferLength = (SYS_MAX_PATH - 1) - wcslen(wcsPortablePath);
        if (sourceLength > bufferLength) { return false; }

        wcsncat(wcsPortablePath, sourcePart, sourceLength);

        // path end reached?
        if ((NULL == longDelim) || (NULL == shortDelim)) { break; }

        // compare the next name
        longPart = longDelim + 1;
        shortPart = shortDelim + 1;
    }

    // Short Path can be safely represented by the US-ASCII Charset.
    return (WideCharToMultiByte(CP_ACP, 0, wcsPortablePath, (-1), destPath, destSize, NULL, NULL) > 0);
}

bool sys_windows_short_path_from_mbs(char *destPath, size_t destSize, const char *mbsLongPath)
{
    // Converting the absolute path in UTF-8 format (MultiByte String)
    // to an alternate (portable) format usable on Windows.
    // Assuming the given paths points to an already existing file or folder.

    wchar_t wcsWidePath[SYS_MAX_PATH];

    if (MultiByteToWideChar(CP_UTF8, 0, mbsLongPath, (-1), wcsWidePath, SYS_MAX_PATH) > 0)
    {
        return sys_windows_short_path_from_wcs(destPath, destSize, wcsWidePath);
    }

    return false;
}

const char *sys_user_path(void)
{
    static char shortPath[SYS_MAX_PATH] = { 0 };
    if ('\0' != shortPath[0]) { return shortPath; }

    WCHAR widePath[SYS_MAX_PATH];

    // "%USERPROFILE%\AppData\Roaming"
    WCHAR *wcsAppDataPath = NULL;
    HRESULT res = SHGetKnownFolderPath(
        &(FOLDERID_RoamingAppData),
        (KF_FLAG_CREATE  | KF_FLAG_DONT_UNEXPAND),
        NULL, &(wcsAppDataPath));

    if (S_OK != res)
    {
        if (NULL != wcsAppDataPath) { CoTaskMemFree(wcsAppDataPath); }
        return NULL;
    }

    LPCWSTR subdirs[] = { L"sm64coopdx", L"sm64ex-coop", L"sm64coopdx", NULL };

    for (int i = 0; NULL != subdirs[i]; i++)
    {
        if (_snwprintf(widePath, SYS_MAX_PATH, L"%s\\%s", wcsAppDataPath, subdirs[i]) <= 0) { return NULL; }

        // Directory already exists.
        if (FALSE != PathIsDirectoryW(widePath))
        {
            // Directory is not empty, so choose this name.
            if (FALSE == PathIsDirectoryEmptyW(widePath)) { break; }
        }

        // 'widePath' will hold the last checked subdir name.
    }

    // System resource can be safely released now.
    if (NULL != wcsAppDataPath) { CoTaskMemFree(wcsAppDataPath); }

    // Always try to create the directory pointed to by User Path,
    // but ignore errors if the destination already exists.
    if (FALSE == CreateDirectoryW(widePath, NULL))
    {
        if (ERROR_ALREADY_EXISTS != GetLastError()) { return NULL; }
    }

    return sys_windows_short_path_from_wcs(shortPath, SYS_MAX_PATH, widePath) ? shortPath : NULL;
}

const char *sys_exe_path(void)
{
    static char shortPath[SYS_MAX_PATH] = { 0 };
    if ('\0' != shortPath[0]) { return shortPath; }

    WCHAR widePath[SYS_MAX_PATH];
    if (0 == GetModuleFileNameW(NULL, widePath, SYS_MAX_PATH)) { return NULL; }

    WCHAR *lastBackslash = wcsrchr(widePath, L'\\');
    if (NULL != lastBackslash) { *lastBackslash = L'\0'; }
    else { return NULL; }

    return sys_windows_short_path_from_wcs(shortPath, SYS_MAX_PATH, widePath) ? shortPath : NULL;
}

static void sys_fatal_impl(const char *msg) {
    MessageBoxA(NULL, msg, "Fatal error", MB_ICONERROR);
    fprintf(stderr, "FATAL ERROR:\n%s\n", msg);
    fflush(stderr);
    exit(1);
}

#elif defined(HAVE_SDL2)

// we can just ask SDL for most of this shit if we have it
#include <SDL2/SDL.h>

#ifdef TARGET_ANDROID
#include "platform.h"
// The purpose of this code is to store/use the game data in /storage/emulated/0
// instead of /storage/emulated/0/Android/data if the user permits it, which
// results in Android not deleting the game data when the app is uninstalled
// This feature was written to accomodate a user who "is unable to install
// updates to apps without first uninstalling the older version, no matter
// which app it is". It is also very useful for people (like me) who 
// frequently switch between the cross-compilation and the Termux build on
// the same device, which necessitates uninstalling the other build's app.
const char *get_gamedir(void) {
    SDL_bool privileged_write = SDL_FALSE, privileged_manage = SDL_FALSE;
    static char gamedir_unprivileged[SYS_MAX_PATH] = { 0 }, gamedir_privileged[SYS_MAX_PATH] = { 0 };
    const char *basedir_unprivileged = SDL_AndroidGetExternalStoragePath();
    const char *basedir_privileged = SDL_AndroidGetTopExternalStoragePath();

    snprintf(gamedir_unprivileged, sizeof(gamedir_unprivileged), 
             "%s", basedir_unprivileged);
    snprintf(gamedir_privileged, sizeof(gamedir_privileged), 
             "%s/%s", basedir_privileged, ANDROID_APPNAME);

    //Android 10 and below
    privileged_write = SDL_AndroidRequestPermission("android.permission.WRITE_EXTERNAL_STORAGE");
    //Android 11 and up
    privileged_manage = SDL_AndroidRequestPermission("android.permission.MANAGE_EXTERNAL_STORAGE");
    return (privileged_write || privileged_manage) ? gamedir_privileged : gamedir_unprivileged;
}

const char *sys_user_path(void) {
    static char path[SYS_MAX_PATH] = { 0 };

    const char *basedir = get_gamedir();
    snprintf(path, sizeof(path), "%s/user", basedir);

    if (!fs_sys_dir_exists(path) && !fs_sys_mkdir(path))
        path[0] = 0; // somehow failed, we got no user path
    return path;
}

const char *sys_exe_path(void) {
    static char path[SYS_MAX_PATH] = { 0 };

    const char *basedir = get_gamedir();
    snprintf(path, sizeof(path), "%s", basedir);

    if (!fs_sys_dir_exists(path) && !fs_sys_mkdir(path))
        path[0] = 0; // somehow failed, we got no exe path
    return path;
}

static void sys_fatal_impl(const char *msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR , "Fatal error", msg, NULL);
    fprintf(stderr, "FATAL ERROR:\n%s\n", msg);
    fflush(stderr);
    exit(1);
}

void sys_fatal(const char *fmt, ...) {
    static char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    fflush(stdout); // push all crap out
    sys_fatal_impl(msg);
}

#else

// TEMPORARY: check the old save folder and copy contents to the new path
// this will be removed after a while
static inline bool copy_userdata(const char *userdir) {
    char oldpath[SYS_MAX_PATH] = { 0 };
    char path[SYS_MAX_PATH] = { 0 };

    // check if a save already exists in the new folder
    snprintf(path, sizeof(path), "%s/" SAVE_FILENAME, userdir);
    if (fs_sys_file_exists(path)) return false;

    // check if a save exists in the old folder ('pc' instead of 'ex')
    strncpy(oldpath, path, sizeof(oldpath));
    const unsigned int len = strlen(userdir);
    oldpath[len - 2] = 'p'; oldpath[len - 1] = 'c';
    if (!fs_sys_file_exists(oldpath)) return false;

    printf("old save detected at '%s', copying to '%s'\n", oldpath, path);

    bool ret = fs_sys_copy_file(oldpath, path);

    // also try to copy the config
    path[len] = oldpath[len] = 0;
    strncat(path, "/" CONFIGFILE_DEFAULT, SYS_MAX_PATH - 1);
    strncat(oldpath, "/" CONFIGFILE_DEFAULT, SYS_MAX_PATH - 1);
    fs_sys_copy_file(oldpath, path);

    return ret;
}

#endif

#endif // platform switch