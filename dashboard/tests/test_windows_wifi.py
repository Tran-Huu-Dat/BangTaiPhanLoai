import ctypes as ct
import unittest
from unittest.mock import Mock, patch

from windows_wifi import Interface, Network, Notification, scan_windows_networks


class WindowsWifiTests(unittest.TestCase):
    def test_native_scan_deduplicates_uses_security_and_frees_buffers(self):
        def native_list(items, item_type):
            buffer = ct.create_string_buffer(8 + len(items) * ct.sizeof(item_type))
            ct.c_uint32.from_buffer(buffer).value = len(items)
            for i, item in enumerate(items):
                ct.memmove(ct.addressof(buffer) + 8 + i * ct.sizeof(item_type), ct.byref(item), ct.sizeof(item))
            return buffer

        def network(ssid, quality, secure):
            item = Network()
            encoded = ssid.encode()
            item.ssid.length = len(encoded)
            item.ssid.value[:len(encoded)] = encoded
            item.quality, item.secure = quality, secure
            return item

        interfaces = native_list([Interface()], Interface)
        networks = native_list([network("BM KTMT", 40, 1), network("BM KTMT", 90, 1),
                                network("Lab nghiên cứu", 60, 0), network("", 99, 0)], Network)
        wlan = Mock()
        for name in ("WlanOpenHandle", "WlanEnumInterfaces", "WlanRegisterNotification",
                     "WlanScan", "WlanGetAvailableNetworkList", "WlanCloseHandle"):
            getattr(wlan, name).return_value = 0

        def output(pointer, buffer):
            ct.cast(pointer, ct.POINTER(ct.c_void_p))[0] = ct.addressof(buffer)
            return 0

        wlan.WlanEnumInterfaces.side_effect = lambda handle, reserved, pointer: output(pointer, interfaces)
        wlan.WlanGetAvailableNetworkList.side_effect = lambda handle, guid, flags, reserved, pointer: output(pointer, networks)

        def scanned(*args):
            callback = wlan.WlanRegisterNotification.call_args.args[3]
            callback(ct.pointer(Notification(source=8, code=7)), None)
            return 0

        wlan.WlanScan.side_effect = scanned
        with patch("windows_wifi.os.name", "nt"), \
                patch("windows_wifi.ct.WinDLL", return_value=wlan, create=True), \
                patch("windows_wifi.ct.WINFUNCTYPE", ct.CFUNCTYPE, create=True):
            self.assertEqual(scan_windows_networks(), [
                {"ssid": "BM KTMT", "rssi": -55, "secure": True},
                {"ssid": "Lab nghiên cứu", "rssi": -70, "secure": False}])
            wlan.WlanScan.side_effect = lambda *args: 0
            with patch("windows_wifi.Event.wait", return_value=False), self.assertRaisesRegex(OSError, "HOST_SCAN_FAILED"):
                scan_windows_networks()
        self.assertEqual(wlan.WlanFreeMemory.call_count, 3)
        self.assertEqual(wlan.WlanCloseHandle.call_count, 2)
        self.assertEqual(wlan.WlanRegisterNotification.call_args.args[1], 0)
