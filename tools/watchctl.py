"""Drive and inspect the GTA-Watch over USB serial.

  python watchctl.py shot [out.png]          screenshot of the watch screen
  python watchctl.py info
  python watchctl.py tap X Y | hold X Y | swipe X0 Y0 X1 Y1
  python watchctl.py page face|map|weather|places|activity|quick|notifications
  python watchctl.py log [seconds]            print serial output
  python watchctl.py raw "<console command>"

Import it to send phone messages (see phonesim.py): Watch().msg(type, bytes)
"""
import base64
import sys
import time
import zlib

import serial

PORT = "COM5"


def open_port(port=PORT, timeout=2):
    # Set DTR/RTS low *before* opening, otherwise the USB-JTAG bridge resets the ESP32.
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = timeout
    s.dtr = False
    s.rts = False
    s.open()
    return s


class Watch:
    def __init__(self, port=PORT):
        self.s = open_port(port, timeout=2)
        time.sleep(0.1)
        self.s.reset_input_buffer()

    def cmd(self, line, expect="OK", timeout=5):
        self.s.write((line + "\n").encode())
        end = time.time() + timeout
        while time.time() < end:
            ln = self.s.readline().decode(errors="replace").strip()
            if ln.startswith(expect) or ln.startswith("ERR"):
                return ln
        return None

    def msg(self, mtype, payload: bytes, chunk=600):
        """Send a phone message in ACKed, checksummed chunks (the USB link drops bytes)."""
        parts = [payload[i:i + chunk] for i in range(0, len(payload), chunk)] or [b""]
        for seq, part in enumerate(parts):
            line = f"m {mtype:02x} {seq} {len(parts)} {zlib.adler32(part):08x} {base64.b64encode(part).decode()}"
            for _ in range(6):
                self.s.reset_input_buffer()
                r = self.cmd(line, expect=f"OK m {seq}", timeout=2)
                if r and r.startswith("OK"):
                    break
            else:
                raise RuntimeError(f"chunk {seq} of message {mtype:#x} not acknowledged: {r}")
        return f"OK {len(payload)}"

    def shot(self, path="shot.png"):
        from PIL import Image

        self.s.reset_input_buffer()
        self.s.write(b"shot\n")
        end = time.time() + 10
        hdr = None
        while time.time() < end:
            ln = self.s.readline().decode(errors="replace").strip()
            if ln.startswith("SHOT "):
                hdr = ln.split()
                break
        if not hdr:
            raise RuntimeError("no screenshot header")
        w, h, n, nlines = (int(v) for v in hdr[1:5])
        lines = {}

        def take(raw):
            parts = raw.strip().split(b" ")
            if len(parts) != 4 or parts[0] != b"L":
                return
            try:
                data = base64.b64decode(parts[3], validate=True)
            except Exception:  # noqa: BLE001
                return
            if zlib.adler32(data) == int(parts[2], 16):
                lines[int(parts[1])] = data

        while True:
            ln = self.s.readline()
            if not ln or ln.strip() == b"END":
                break
            take(ln)
        for _ in range(5):  # re-request anything lost or corrupted
            missing = [i for i in range(nlines) if i not in lines]
            if not missing:
                break
            for i in missing:
                self.s.write(f"shotline {i}\n".encode())
                take(self.s.readline())
        if len(lines) != nlines:
            raise RuntimeError(f"screenshot incomplete: {len(lines)}/{nlines} lines")
        rle = b"".join(lines[i] for i in range(nlines))
        if len(rle) != n:
            raise RuntimeError(f"got {len(rle)} of {n} bytes")
        rgb = bytearray()
        lut = {}
        for i in range(0, n, 3):
            v = rle[i + 1] | (rle[i + 2] << 8)
            px = lut.get(v)
            if px is None:
                px = lut[v] = bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
            rgb += px * rle[i]
        if len(rgb) != w * h * 3:
            raise RuntimeError("pixel count mismatch")
        Image.frombytes("RGB", (w, h), bytes(rgb)).save(path)
        return path


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    if a[0] == "log":
        s = open_port(PORT, timeout=0.5)
        end = time.time() + float(a[1] if len(a) > 1 else 10)
        while time.time() < end:
            ln = s.readline()
            if ln:
                print(ln.decode(errors="replace").rstrip())
        return
    w = Watch()
    if a[0] == "shot":
        print(w.shot(a[1] if len(a) > 1 else "shot.png"))
    elif a[0] == "info":
        print(w.cmd("info", expect="INFO"))
    elif a[0] in ("tap", "hold", "swipe", "page", "wake", "sleep"):
        print(w.cmd(" ".join(a)))
    elif a[0] == "raw":
        print(w.cmd(a[1], expect=""))


if __name__ == "__main__":
    main()
