"""
MQTT stress test:
  - 500 rapid commands (ON/OFF/TOGGLE/PULSE) at ~10/sec
  - 5 broker-disconnect simulations (stop subscribing, reconnect)
  - Reports any publish errors
"""
import paho.mqtt.client as mqtt
import time, sys

BROKER = "10.0.0.19"
PORT   = 1883
BASE   = "espnow"

connected = False
errors = 0

def on_connect(c, ud, flags, rc, props=None):
    global connected
    connected = (rc == 0)
    print(f"MQTT {'connected' if connected else 'FAILED rc='+str(rc)}")

def on_disconnect(c, ud, flags, rc, props=None):
    global connected
    connected = False
    print(f"MQTT disconnected rc={rc}")

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.on_connect    = on_connect
c.on_disconnect = on_disconnect
c.username_pw_set("test", "test")
c.connect(BROKER, PORT, keepalive=60)
c.loop_start()

time.sleep(1)
if not connected:
    print("Cannot connect to broker — aborting")
    sys.exit(1)

print("=== Phase 1: rapid command flood (500 commands) ===")
cmds = [
    (f"{BASE}/gpio/4/command", "ON"),
    (f"{BASE}/gpio/4/command", "OFF"),
    (f"{BASE}/gpio/5/command", "ON"),
    (f"{BASE}/gpio/5/command", "OFF"),
    (f"{BASE}/gpio/4/command", "TOGGLE"),
    (f"{BASE}/gpio/control",   '{"pin":6,"pulses":2}'),
]
for i in range(500):
    topic, payload = cmds[i % len(cmds)]
    r = c.publish(topic, payload, qos=0)
    if r.rc != 0:
        errors += 1
    if i % 50 == 0:
        print(f"  {i}/500 sent, errors={errors}")
    time.sleep(0.1)

print(f"Phase 1 done. Total publish errors: {errors}")

print("=== Phase 2: disconnect/reconnect cycles (5x) ===")
for i in range(5):
    c.disconnect()
    time.sleep(2)
    c.connect(BROKER, PORT, keepalive=60)
    time.sleep(3)
    print(f"  Cycle {i+1}/5 — connected={connected}")
    # Send a command after each reconnect
    c.publish(f"{BASE}/gpio/control", '{"pin":6,"pulses":1}', qos=0)
    time.sleep(1)

print("=== Phase 3: sustained commands over 30s ===")
t_end = time.time() + 30
count = 0
while time.time() < t_end:
    c.publish(f"{BASE}/gpio/4/command", "TOGGLE", qos=0)
    c.publish(f"{BASE}/gpio/control", '{"pin":6,"pulses":1}', qos=0)
    count += 2
    time.sleep(0.5)
print(f"Phase 3 done. {count} commands sent.")

print(f"\n=== STRESS TEST COMPLETE — total publish errors: {errors} ===")
c.loop_stop()
c.disconnect()
