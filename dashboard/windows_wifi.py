"""Read nearby Windows WLAN networks without changing the PC's connection."""
import ctypes as ct
import os
import re
import subprocess
from threading import Event


DWORD = ct.c_uint32
GUID = ct.c_ubyte * 16


class Interface(ct.Structure):
    _fields_ = [("guid", GUID), ("description", ct.c_ubyte * 512), ("state", DWORD)]


class Ssid(ct.Structure):
    _fields_ = [("length", DWORD), ("value", ct.c_ubyte * 32)]


class Network(ct.Structure):
    _fields_ = [("profile", ct.c_ubyte * 512), ("ssid", Ssid), ("bss_type", DWORD),
                ("bssid_count", DWORD), ("connectable", DWORD), ("reason", DWORD),
                ("phy_count", DWORD), ("phy_types", DWORD * 8), ("more_phy", DWORD),
                ("quality", DWORD), ("secure", DWORD), ("auth", DWORD), ("cipher", DWORD),
                ("flags", DWORD), ("reserved", DWORD)]


class Notification(ct.Structure):
    _fields_ = [("source", DWORD), ("code", DWORD), ("guid", GUID),
                ("size", DWORD), ("data", ct.c_void_p)]


def _check(code):
    if code:
        raise ct.WinError(code)


def _entries(pointer, item_type):
    count = ct.cast(pointer, ct.POINTER(DWORD)).contents.value
    return (item_type * count).from_address(pointer.value + 8)


def scan_windows_networks():
    if os.name != "nt":
        raise OSError("WINDOWS_WIFI_ONLY")
    wlan = ct.WinDLL("wlanapi.dll")
    wlan.WlanOpenHandle.argtypes = [DWORD, ct.c_void_p, ct.POINTER(DWORD), ct.POINTER(ct.c_void_p)]
    wlan.WlanEnumInterfaces.argtypes = [ct.c_void_p, ct.c_void_p, ct.POINTER(ct.c_void_p)]
    wlan.WlanScan.argtypes = [ct.c_void_p, ct.POINTER(GUID), ct.c_void_p, ct.c_void_p, ct.c_void_p]
    wlan.WlanGetAvailableNetworkList.argtypes = [ct.c_void_p, ct.POINTER(GUID), DWORD,
                                                ct.c_void_p, ct.POINTER(ct.c_void_p)]
    wlan.WlanRegisterNotification.argtypes = [ct.c_void_p, DWORD, DWORD, ct.c_void_p,
                                             ct.c_void_p, ct.c_void_p, ct.POINTER(DWORD)]
    wlan.WlanFreeMemory.argtypes = [ct.c_void_p]
    wlan.WlanFreeMemory.restype = None
    wlan.WlanCloseHandle.argtypes = [ct.c_void_p, ct.c_void_p]
    handle, interfaces, version = ct.c_void_p(), ct.c_void_p(), DWORD()
    _check(wlan.WlanOpenHandle(2, None, ct.byref(version), ct.byref(handle)))
    try:
        _check(wlan.WlanEnumInterfaces(handle, None, ct.byref(interfaces)))
        adapters = _entries(interfaces, Interface)
        if not adapters:
            raise OSError("NO_WIFI_ADAPTER")
        done, outcome, active_guid = Event(), [], None

        @ct.WINFUNCTYPE(None, ct.POINTER(Notification), ct.c_void_p)
        def notified(pointer, context):
            info = pointer.contents
            if info.source == 8 and info.code in (7, 8) and bytes(info.guid) == active_guid:
                outcome.append(info.code)
                done.set()

        _check(wlan.WlanRegisterNotification(handle, 8, 0, notified, None, None, None))
        networks = {}
        for adapter in adapters:
            active_guid = bytes(adapter.guid)
            done.clear()
            outcome.clear()
            _check(wlan.WlanScan(handle, ct.byref(adapter.guid), None, None, None))
            if not done.wait(5) or outcome[-1] != 7:
                raise OSError("HOST_SCAN_FAILED")
            found = ct.c_void_p()
            _check(wlan.WlanGetAvailableNetworkList(handle, ct.byref(adapter.guid), 0,
                                                   None, ct.byref(found)))
            try:
                for net in _entries(found, Network):
                    ssid = bytes(net.ssid.value[:min(net.ssid.length, 32)]).decode("utf-8", "replace")
                    if not ssid:
                        continue
                    rssi = round(min(net.quality, 100) / 2 - 100)
                    key = (ssid, bool(net.secure))
                    if key not in networks or networks[key]["rssi"] < rssi:
                        networks[key] = {"ssid": ssid, "rssi": rssi, "secure": bool(net.secure)}
            finally:
                wlan.WlanFreeMemory(found)
        return sorted(networks.values(), key=lambda net: -net["rssi"])[:16]
    finally:
        # Unregister before releasing the Python callback and native handle.
        wlan.WlanRegisterNotification(handle, 0, 0, None, None, None, None)
        if interfaces:
            wlan.WlanFreeMemory(interfaces)
        wlan.WlanCloseHandle(handle, None)


_SSID_RE = re.compile(r"^\s*SSID\s*:\s*(.+?)\s*$", re.MULTILINE)
_KEY_RE = re.compile(r"^\s*Key Content\s*:\s*(.*?)\s*$", re.MULTILINE)


def get_current_windows_ssid() -> str | None:
    if os.name != "nt":
        return None
    try:
        proc = subprocess.run(
            ["netsh", "wlan", "show", "interfaces"],
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if proc.returncode != 0:
        return None
    m = _SSID_RE.search(proc.stdout or "")
    ssid = m.group(1).strip() if m else ""
    return ssid or None


def get_current_windows_wifi() -> tuple[str | None, str | None]:
    ssid = get_current_windows_ssid()
    if not ssid:
        return None, None
    try:
        proc = subprocess.run(
            ["netsh", "wlan", "show", "profile", f"name={ssid}", "key=clear"],
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return ssid, None
    if proc.returncode != 0:
        return ssid, ""
    m = _KEY_RE.search(proc.stdout or "")
    return ssid, (m.group(1) if m else "")

