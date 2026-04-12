"""Capture serial output to file for stress test duration."""
import serial, time, sys, io

# Force UTF-8 output on Windows
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

LOG = "stress_serial.log"
DURATION = 120  # seconds

s = serial.Serial('COM16', 115200, timeout=0.5)
time.sleep(0.3)
deadline = time.time() + DURATION
with open(LOG, 'w', encoding='utf-8', errors='replace') as f:
    sys.stdout.write(f"Logging to {LOG} for {DURATION}s ...\n")
    sys.stdout.flush()
    while time.time() < deadline:
        line = s.readline()
        if line:
            txt = line.decode('utf-8', errors='replace').rstrip()
            ts = f"[{time.time():.1f}] {txt}"
            sys.stdout.write(ts + '\n')
            sys.stdout.flush()
            f.write(ts + '\n')
            f.flush()
    sys.stdout.write("Done.\n")
s.close()
