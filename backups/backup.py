# Segmented flash backup: long esptool reads drop out on this watch's USB-JTAG port.
import subprocess, sys, os
PY = r"E:\GTA-Nav\.pio-core\penv\Scripts\python.exe"
OUT = r"G:\GTA-Watch\backups\watch-full-20260930.bin"
SEG = 0x40000
TOTAL = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x2000000
tmp = OUT + ".seg"
with open(OUT, "wb") as out:
    for addr in range(0, TOTAL, SEG):
        for attempt in range(5):
            r = subprocess.run([PY, "-m", "esptool", "--port", "COM5", "--before", "default-reset",
                                "--after", "no-reset", "read-flash", hex(addr), hex(SEG), tmp],
                               capture_output=True, text=True)
            if r.returncode == 0 and os.path.getsize(tmp) == SEG:
                break
        else:
            open(OUT + ".log", "a").write(f"FAILED at {addr:#x}\n{r.stdout[-600:]}\n{r.stderr[-600:]}\n"); sys.exit(1)
        out.write(open(tmp, "rb").read())
        open(OUT + ".log", "a").write(f"ok {addr:#010x}\n")
os.remove(tmp)
subprocess.run([PY, "-m", "esptool", "--port", "COM5", "--after", "hard-reset", "chip-id"], capture_output=True)
print("DONE", OUT)
