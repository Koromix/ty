/**
 * ty, command-line program to manage Teensy devices
 * http://github.com/Koromix/ty
 * Copyright (C) 2014 Niels Martignène
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <shlobj.h>
#include "system.h"

static const uint64_t delta_epoch = 11644473600000;

static DWORD orig_mode;

char *ty_win32_strerror(DWORD err)
{
    static char buf[2048];
    char *ptr;
    DWORD r;

    if (!err)
        err = GetLastError();

    r = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                      err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), NULL);

    if (r) {
        ptr = buf + strlen(buf);
        // FormatMessage adds newlines, remove them
        while (ptr > buf && (ptr[-1] == '\n' || ptr[-1] == '\r'))
            ptr--;
        *ptr = 0;
    } else {
        strcpy(buf, "(unknown)");
    }

    return buf;
}

bool ty_win32_test_version(ty_win32_version version)
{
    OSVERSIONINFOEX info = {0};
    DWORDLONG cond = 0;

    switch (version) {
    case TY_WIN32_XP:
        info.dwMajorVersion = 5;
        info.dwMinorVersion = 1;
        break;
    case TY_WIN32_VISTA:
        info.dwMajorVersion = 6;
        break;
    case TY_WIN32_SEVEN:
        info.dwMajorVersion = 6;
        info.dwMinorVersion = 1;
        break;
    }

    VER_SET_CONDITION(cond, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(cond, VER_MINORVERSION, VER_GREATER_EQUAL);

    return VerifyVersionInfo(&info, VER_MAJORVERSION | VER_MINORVERSION, cond);
}

uint64_t ty_millis(void)
{
    FILETIME ft;
    uint64_t millis;

    GetSystemTimeAsFileTime(&ft);

    millis = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    millis = (millis / 10000) - delta_epoch;

    return millis;
}

void ty_delay(unsigned int ms)
{
    Sleep(ms);
}

static int get_special_folder(char **rpath, int folder, const char *name, bool def)
{
    char *path;
    char buf[MAX_PATH];
    HRESULT hr;
    int r;

    hr = SHGetFolderPath(NULL, folder, def ? (HANDLE)-1 : NULL, SHGFP_TYPE_CURRENT, buf);
    if (FAILED(hr))
        return ty_error(TY_ERROR_SYSTEM, "SHGetFolderPath(%d) failed: %s", folder,
                        ty_win32_strerror(0));

    if (name) {
        r = asprintf(&path, "%s\\%s", buf, name);
        if (r < 0)
            return ty_error(TY_ERROR_MEMORY, NULL);
    } else {
        path = strdup(buf);
        if (!path)
            return ty_error(TY_ERROR_MEMORY, NULL);
    }

    *rpath = path;
    if (_access(path, 0))
        return 0;
    return 1;
}

int ty_find_config(char **rpath, const char *name)
{
    assert(rpath);
    assert(name && name[0]);

#define SPECIAL(folder,def) \
        r = get_special_folder(&path, (folder), name, (def)); \
        if (r) { \
            if (r == 1) \
                *rpath = path; \
            return r; \
        } \
        free(path);

    char *path;
    int r;

    SPECIAL(CSIDL_LOCAL_APPDATA, false);
    SPECIAL(CSIDL_LOCAL_APPDATA, true);
    SPECIAL(CSIDL_COMMON_APPDATA, false);

#undef SPECIAL

    return 0;
}

int ty_user_config(char **rpath, const char *name, bool make_parents)
{
    assert(rpath);
    assert(name && name[0]);

    char *path;
    int r;

    r = get_special_folder(&path, CSIDL_LOCAL_APPDATA, name, false);
    if (r < 0)
        return r;

    if (make_parents) {
        r = ty_mkdir(path, 0755, TY_MKDIR_OMIT_LAST | TY_MKDIR_MAKE_PARENTS | TY_MKDIR_IGNORE_EXISTS);
        if (r < 0)
            goto error;
    }

    return 0;

error:
    free(path);
    return r;
}

static void restore_terminal(void)
{
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), orig_mode);
}

int ty_terminal_change(uint32_t flags)
{
    HANDLE handle;
    DWORD mode;
    BOOL r;

    handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
        return ty_error(TY_ERROR_SYSTEM, "GetStdHandle(STD_INPUT_HANDLE) failed");

    r = GetConsoleMode(handle, &mode);
    if (!r) {
        if (GetLastError() == ERROR_INVALID_HANDLE)
            return ty_error(TY_ERROR_UNSUPPORTED, "Not a terminal");
        return ty_error(TY_ERROR_SYSTEM, "GetConsoleMode(STD_INPUT_HANDLE) failed: %s",
                        ty_win32_strerror(0));
    }

    static bool saved = false;
    if (!saved) {
        orig_mode = mode;
        saved = true;

        atexit(restore_terminal);
    }

    mode = ENABLE_PROCESSED_INPUT;
    if (!(flags & TY_TERMINAL_RAW))
        mode |= ENABLE_LINE_INPUT;
    if (!(flags & TY_TERMINAL_SILENT))
        mode |= ENABLE_ECHO_INPUT;

    r = SetConsoleMode(handle, mode);
    if (!r)
        return ty_error(TY_ERROR_SYSTEM, "SetConsoleMode(STD_INPUT_HANDLE) failed: %s",
                        ty_win32_strerror(0));

    return 0;
}
