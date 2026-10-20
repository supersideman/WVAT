"""传输层：把"一行一帧"的读写抽象出来，便于替换物理链路。

- SocketChannel  : 本地 TCP，模拟蓝牙/USB 串口，**无需驱动即可联调**（P0 用这个）
- SerialChannel  : 真实串口（USB Type-C / 蓝牙 SPP 虚拟 COM 口），需要 pyserial

两者对外接口一致：send_line() / recv_lines() / close()
"""

from __future__ import annotations

import socket
from typing import List, Optional

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765


class _BaseChannel:
    def send_line(self, line: str) -> None:  # pragma: no cover
        raise NotImplementedError

    def recv_lines(self) -> List[str]:       # pragma: no cover
        raise NotImplementedError

    def close(self) -> None:                 # pragma: no cover
        pass


class SocketServerChannel(_BaseChannel):
    """代理侧：监听一个端口，接受一台"手表"接入。"""

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, quiet: bool = False):
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind((host, port))
        self._srv.listen(1)
        self._srv.setblocking(False)
        self._conn: Optional[socket.socket] = None
        self._buf = b""
        self._quiet = quiet
        self.host, self.port = host, port
        if not quiet:
            print(f"[transport] 等待终端接入 tcp://{host}:{port} …", flush=True)

    @property
    def connected(self) -> bool:
        return self._conn is not None

    def _try_accept(self) -> None:
        if self._conn is not None:
            return
        try:
            conn, addr = self._srv.accept()
        except BlockingIOError:
            return
        conn.setblocking(False)
        self._conn = conn
        self._buf = b""
        if not self._quiet:
            print(f"[transport] 终端已连接 {addr}", flush=True)

    def send_line(self, line: str) -> None:
        if self._conn is None:
            return
        try:
            self._conn.sendall(line.encode("utf-8"))
        except OSError:
            self.close()

    def recv_lines(self) -> List[str]:
        self._try_accept()
        if self._conn is None:
            return []
        try:
            chunk = self._conn.recv(4096)
        except BlockingIOError:
            return []
        except OSError:
            self.close()
            return []
        if not chunk:
            # 对端关闭
            if not self._quiet:
                print("[transport] 终端断开", flush=True)
            self.close()
            return []
        self._buf += chunk
        return _split_lines(self)

    def close(self) -> None:
        if self._conn is not None:
            try:
                self._conn.close()
            except OSError:
                pass
            self._conn = None
        self._buf = b""

    def shutdown(self) -> None:
        self.close()
        try:
            self._srv.close()
        except OSError:
            pass


class SocketClientChannel(_BaseChannel):
    """终端侧（模拟手表）：连到代理。"""

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, retries: int = 40):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        last_err: Optional[Exception] = None
        for _ in range(retries):
            try:
                self._sock.connect((host, port))
                last_err = None
                break
            except OSError as exc:      # 代理可能还没起来，稍等重试
                last_err = exc
                import time
                time.sleep(0.1)
        if last_err is not None:
            raise ConnectionError(f"连不上 {host}:{port} —— {last_err}")
        self._sock.setblocking(False)
        self._buf = b""

    def send_line(self, line: str) -> None:
        self._sock.sendall(line.encode("utf-8"))

    def recv_lines(self) -> List[str]:
        try:
            chunk = self._sock.recv(4096)
        except BlockingIOError:
            return []
        except OSError:
            return []
        if not chunk:
            return []
        self._buf += chunk
        return _split_lines(self)

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


class SerialChannel(_BaseChannel):
    """真实串口。需要 pip install pyserial（不属于当前 P0 阶段）。"""

    def __init__(self, port: str, baudrate: int = 115200):
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise RuntimeError("串口模式需要 pyserial：pip install pyserial") from exc
        self._ser = serial.Serial(port, baudrate, timeout=0)
        self._buf = b""

    def send_line(self, line: str) -> None:
        self._ser.write(line.encode("utf-8"))

    def recv_lines(self) -> List[str]:
        n = self._ser.in_waiting
        if n:
            self._buf += self._ser.read(n)
        return _split_lines(self)

    def close(self) -> None:
        try:
            self._ser.close()
        except Exception:
            pass


def _split_lines(ch: _BaseChannel) -> List[str]:
    """按 \\r\\n（或单独的 \\n）切行，剩余半包留在缓冲里。"""
    out: List[str] = []
    while True:
        idx_n = ch._buf.find(b"\n")
        if idx_n < 0:
            break
        raw = ch._buf[: idx_n + 1]
        ch._buf = ch._buf[idx_n + 1:]
        try:
            text = raw.decode("utf-8", "replace")
        except Exception:
            continue
        text = text.rstrip("\r\n")
        if text:
            out.append(text)
    return out
