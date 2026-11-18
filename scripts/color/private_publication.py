"""Exclusive evidence publication with owner-only POSIX modes or Windows ACLs."""
from __future__ import annotations

import os
from pathlib import Path


def _windows_api():
    import ctypes
    from ctypes import wintypes
    from types import SimpleNamespace

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    security = ctypes.WinDLL("advapi32", use_last_error=True)
    pointer = ctypes.c_void_p
    dword_pointer = ctypes.POINTER(wintypes.DWORD)
    pointer_pointer = ctypes.POINTER(pointer)
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    kernel.LocalFree.argtypes = [pointer]
    kernel.LocalFree.restype = pointer
    security.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    security.OpenProcessToken.restype = wintypes.BOOL
    security.GetTokenInformation.argtypes = [wintypes.HANDLE, wintypes.DWORD, pointer, wintypes.DWORD, dword_pointer]
    security.GetTokenInformation.restype = wintypes.BOOL
    security.ConvertSidToStringSidW.argtypes = [pointer, ctypes.POINTER(wintypes.LPWSTR)]
    security.ConvertSidToStringSidW.restype = wintypes.BOOL
    security.ConvertStringSidToSidW.argtypes = [wintypes.LPCWSTR, pointer_pointer]
    security.ConvertStringSidToSidW.restype = wintypes.BOOL
    security.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, pointer_pointer, dword_pointer]
    security.ConvertStringSecurityDescriptorToSecurityDescriptorW.restype = wintypes.BOOL
    security.GetNamedSecurityInfoW.argtypes = [wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD,
                                             pointer_pointer, pointer_pointer, pointer_pointer, pointer_pointer, pointer_pointer]
    security.GetNamedSecurityInfoW.restype = wintypes.DWORD
    security.GetSecurityDescriptorControl.argtypes = [pointer, ctypes.POINTER(wintypes.WORD), dword_pointer]
    security.GetSecurityDescriptorControl.restype = wintypes.BOOL
    security.GetAclInformation.argtypes = [pointer, pointer, wintypes.DWORD, wintypes.DWORD]
    security.GetAclInformation.restype = wintypes.BOOL
    security.GetAce.argtypes = [pointer, wintypes.DWORD, pointer_pointer]
    security.GetAce.restype = wintypes.BOOL
    security.EqualSid.argtypes = [pointer, pointer]
    security.EqualSid.restype = wintypes.BOOL

    class SecurityAttributes(ctypes.Structure):
        _fields_ = [("length", wintypes.DWORD), ("descriptor", pointer), ("inherit", wintypes.BOOL)]

    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.POINTER(SecurityAttributes), wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    return SimpleNamespace(c=ctypes, w=wintypes, kernel=kernel, security=security, attributes=SecurityAttributes)


def _user_sid(api) -> str:
    token = api.w.HANDLE()
    if not api.security.OpenProcessToken(api.kernel.GetCurrentProcess(), 0x0008, api.c.byref(token)):
        raise api.c.WinError()
    converted = api.w.LPWSTR()
    try:
        needed = api.w.DWORD()
        api.security.GetTokenInformation(token, 1, None, 0, api.c.byref(needed))  # TokenUser
        if not needed.value:
            raise api.c.WinError()
        information = api.c.create_string_buffer(needed.value)
        if not api.security.GetTokenInformation(token, 1, information, needed, api.c.byref(needed)):
            raise api.c.WinError()
        # TOKEN_USER begins with SID_AND_ATTRIBUTES, whose first member is PSID.
        sid = api.c.cast(information, api.c.POINTER(api.c.c_void_p))[0]
        if not api.security.ConvertSidToStringSidW(sid, api.c.byref(converted)):
            raise api.c.WinError()
        return converted.value
    finally:
        if converted:
            api.kernel.LocalFree(api.c.cast(converted, api.c.c_void_p))
        api.kernel.CloseHandle(token)


def _windows_create_private(path: Path) -> int:
    import msvcrt

    api = _windows_api()
    sid = _user_sid(api)
    descriptor = api.c.c_void_p()
    # Protected DACL: no inherited access and one full-control ACE for this user.
    if not api.security.ConvertStringSecurityDescriptorToSecurityDescriptorW(
            f"O:{sid}D:P(A;;FA;;;{sid})", 1, api.c.byref(descriptor), None):
        raise api.c.WinError()
    try:
        attributes = api.attributes(api.c.sizeof(api.attributes), descriptor, False)
        handle = api.kernel.CreateFileW(str(path), 0x40000000, 0, api.c.byref(attributes), 1, 0x80, None)
        if handle == api.c.c_void_p(-1).value:
            error = api.c.get_last_error()
            if error in (80, 183):
                raise FileExistsError(error, "output already exists", str(path))
            raise api.c.WinError(error)
        try:
            return msvcrt.open_osfhandle(handle, os.O_WRONLY | os.O_BINARY)
        except BaseException:
            api.kernel.CloseHandle(handle)
            raise
    finally:
        api.kernel.LocalFree(descriptor)


def is_private_file(path: Path) -> bool:
    """Inspect the actual published security boundary, not Windows mode emulation."""
    if path.is_symlink() or not path.is_file():
        return False
    if os.name != "nt":
        return path.stat().st_mode & 0o777 == 0o600 and path.stat().st_uid == os.getuid()
    api = _windows_api()
    owner = api.c.c_void_p()
    dacl = api.c.c_void_p()
    descriptor = api.c.c_void_p()
    current_sid = api.c.c_void_p()
    try:
        result = api.security.GetNamedSecurityInfoW(str(path), 1, 0x00000005,
            api.c.byref(owner), None, api.c.byref(dacl), None, api.c.byref(descriptor))
        if result:
            raise api.c.WinError(result)
        if not api.security.ConvertStringSidToSidW(_user_sid(api), api.c.byref(current_sid)):
            raise api.c.WinError()
        control, revision = api.w.WORD(), api.w.DWORD()
        if not api.security.GetSecurityDescriptorControl(descriptor, api.c.byref(control), api.c.byref(revision)):
            raise api.c.WinError()
        if not owner or not dacl or not control.value & 0x1000 or not api.security.EqualSid(owner, current_sid):
            return False

        class AclSizeInformation(api.c.Structure):
            _fields_ = [("count", api.w.DWORD), ("used", api.w.DWORD), ("free", api.w.DWORD)]

        size = AclSizeInformation()
        if not api.security.GetAclInformation(dacl, api.c.byref(size), api.c.sizeof(size), 2):
            raise api.c.WinError()
        if size.count != 1:
            return False
        ace = api.c.c_void_p()
        if not api.security.GetAce(dacl, 0, api.c.byref(ace)):
            raise api.c.WinError()
        ace_type = api.c.cast(ace, api.c.POINTER(api.w.BYTE))[0]
        rights = api.c.cast(ace.value + 4, api.c.POINTER(api.w.DWORD))[0]
        return ace_type == 0 and rights == 0x001F01FF and bool(api.security.EqualSid(ace.value + 8, current_sid))
    finally:
        if current_sid:
            api.kernel.LocalFree(current_sid)
        if descriptor:
            api.kernel.LocalFree(descriptor)


def publish_new_file(path: Path, payload: bytes) -> None:
    """Publish only a new path; permissions exist before any evidence is written."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = _windows_create_private(path) if os.name == "nt" else os.open(
        path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise
