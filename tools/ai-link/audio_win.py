"""Windows CoreAudio 主音量控制（纯 ctypes，无需第三方库）。

为什么不用模拟音量键：
    实测同一个 `SendInput` 调用，在前台进程里能改音量，在后台长时间运行的
    网关进程里却"返回成功但毫无效果"。按键注入依赖输入桌面/焦点这套机制，
    不可靠也不可验证。
    直接调 IAudioEndpointVolume 则是**确定性**的，而且能把音量读回来做验证。

用到的 COM 接口（vtable 顺序见注释，改动需谨慎）：
    IMMDeviceEnumerator → GetDefaultAudioEndpoint
    IMMDevice           → Activate(IID_IAudioEndpointVolume)
    IAudioEndpointVolume→ SetMasterVolumeLevelScalar / GetMasterVolumeLevelScalar
                          SetMute / GetMute
"""
from __future__ import annotations

import ctypes
import sys
from ctypes import POINTER, byref, c_float, c_int, c_ubyte, c_ulong, c_ushort, c_void_p

HRESULT = ctypes.c_long

CLSCTX_INPROC_SERVER = 1
eRender = 0
eConsole = 0


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", c_ulong),
        ("Data2", c_ushort),
        ("Data3", c_ushort),
        ("Data4", c_ubyte * 8),
    ]

    def __repr__(self) -> str:  # 便于调试
        return f"GUID({self.Data1:08X}-...)" 


CLSID_MMDeviceEnumerator = GUID(
    0xBCDE0395, 0xE52F, 0x467C,
    (c_ubyte * 8)(0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E),
)
IID_IMMDeviceEnumerator = GUID(
    0xA95664D2, 0x9614, 0x4F35,
    (c_ubyte * 8)(0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6),
)
IID_IAudioEndpointVolume = GUID(
    0x5CDF2C82, 0x841E, 0x4546,
    (c_ubyte * 8)(0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A),
)

_ole32 = ctypes.windll.ole32
_ole32.CoInitializeEx.argtypes = [c_void_p, c_ulong]
_ole32.CoInitializeEx.restype = HRESULT
_ole32.CoCreateInstance.argtypes = [
    POINTER(GUID), c_void_p, c_ulong, POINTER(GUID), POINTER(c_void_p),
]
_ole32.CoCreateInstance.restype = HRESULT


def _vtbl(pobj: c_void_p, index: int, restype, argtypes, *args):
    """按 vtable 下标调用 COM 方法（第 0/1/2 项是 QueryInterface/AddRef/Release）。"""
    vtbl = ctypes.cast(pobj, POINTER(POINTER(c_void_p))).contents
    proto = ctypes.WINFUNCTYPE(restype, c_void_p, *argtypes)
    fn = proto(vtbl[index])
    return fn(pobj, *args)


def _release(pobj: c_void_p) -> None:
    if pobj:
        _vtbl(pobj, 2, c_ulong, [])


class _EndpointVolume:
    """持有 IAudioEndpointVolume 指针的轻量包装。"""

    # vtable 下标
    SET_SCALAR = 7
    GET_SCALAR = 9
    SET_MUTE = 14
    GET_MUTE = 15

    def __init__(self) -> None:
        if sys.platform != "win32":
            raise RuntimeError("CoreAudio 仅 Windows 可用")

        _ole32.CoInitializeEx(None, 0)
        self._enum = c_void_p()
        hr = _ole32.CoCreateInstance(
            byref(CLSID_MMDeviceEnumerator), None, CLSCTX_INPROC_SERVER,
            byref(IID_IMMDeviceEnumerator), byref(self._enum),
        )
        if hr < 0 or not self._enum:
            raise RuntimeError(f"创建 MMDeviceEnumerator 失败 hr=0x{hr & 0xFFFFFFFF:08X}")

        # IMMDeviceEnumerator::GetDefaultAudioEndpoint = 4
        self._dev = c_void_p()
        hr = _vtbl(self._enum, 4, HRESULT, [c_int, c_int, POINTER(c_void_p)],
                   eRender, eConsole, byref(self._dev))
        if hr < 0 or not self._dev:
            raise RuntimeError(f"取默认播放设备失败 hr=0x{hr & 0xFFFFFFFF:08X}")

        # IMMDevice::Activate = 3
        self._vol = c_void_p()
        hr = _vtbl(self._dev, 3, HRESULT,
                   [POINTER(GUID), c_ulong, c_void_p, POINTER(c_void_p)],
                   byref(IID_IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
                   None, byref(self._vol))
        if hr < 0 or not self._vol:
            raise RuntimeError(f"激活 IAudioEndpointVolume 失败 hr=0x{hr & 0xFFFFFFFF:08X}")

    # ── 音量 ──────────────────────────────────────────
    def get_scalar(self) -> float:
        v = c_float(0.0)
        hr = _vtbl(self._vol, self.GET_SCALAR, HRESULT, [POINTER(c_float)], byref(v))
        if hr < 0:
            raise RuntimeError(f"读音量失败 hr=0x{hr & 0xFFFFFFFF:08X}")
        return float(v.value)

    def set_scalar(self, value: float) -> None:
        value = max(0.0, min(1.0, float(value)))
        hr = _vtbl(self._vol, self.SET_SCALAR, HRESULT,
                   [c_float, POINTER(GUID)], c_float(value), None)
        if hr < 0:
            raise RuntimeError(f"设音量失败 hr=0x{hr & 0xFFFFFFFF:08X}")

    # ── 静音 ──────────────────────────────────────────
    def get_mute(self) -> bool:
        m = c_int(0)
        hr = _vtbl(self._vol, self.GET_MUTE, HRESULT, [POINTER(c_int)], byref(m))
        if hr < 0:
            raise RuntimeError(f"读静音状态失败 hr=0x{hr & 0xFFFFFFFF:08X}")
        return bool(m.value)

    def set_mute(self, mute: bool) -> None:
        hr = _vtbl(self._vol, self.SET_MUTE, HRESULT,
                   [c_int, POINTER(GUID)], c_int(1 if mute else 0), None)
        if hr < 0:
            raise RuntimeError(f"设静音失败 hr=0x{hr & 0xFFFFFFFF:08X}")

    def close(self) -> None:
        _release(self._vol)
        _release(self._dev)
        _release(self._enum)


# ── 便捷函数（每次调用建一个对象；音量为毫秒级操作，开销可忽略）──
def get_volume() -> float:
    e = _EndpointVolume()
    try:
        return e.get_scalar()
    finally:
        e.close()


def set_volume(value: float) -> float:
    e = _EndpointVolume()
    try:
        e.set_scalar(value)
        return e.get_scalar()          # 回读，确认真的生效
    finally:
        e.close()


def step_volume(delta: float) -> float:
    """在当前音量上增减，返回回读值。"""
    e = _EndpointVolume()
    try:
        cur = e.get_scalar()
        e.set_scalar(cur + delta)
        if e.get_mute() and delta > 0:
            e.set_mute(False)          # 调大音量时顺手取消静音
        return e.get_scalar()
    finally:
        e.close()


def get_mute() -> bool:
    e = _EndpointVolume()
    try:
        return e.get_mute()
    finally:
        e.close()


def set_mute(mute: bool) -> None:
    e = _EndpointVolume()
    try:
        e.set_mute(mute)
    finally:
        e.close()
