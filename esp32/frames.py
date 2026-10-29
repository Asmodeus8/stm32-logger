# Frame helpers shared by the ESP32 bridge and its tests (runs on MicroPython and CPython).


def checksum(body):
    c = 0
    for ch in body:
        c ^= ord(ch)
    return c


def build(body):
    return "$%s*%02X\r\n" % (body, checksum(body))


def parse_log(line):
    """Returns a dict for a valid $LOG frame, or None."""
    line = line.strip()
    if not line.startswith("$") or "*" not in line:
        return None
    body, _, given = line[1:].partition("*")
    try:
        if int(given, 16) != checksum(body):
            return None
    except ValueError:
        return None
    parts = body.split(",")
    if parts[0] != "LOG" or len(parts) != 8:
        return None
    try:
        seq, epoch, t, rh, pa, lm, flags = (int(p) for p in parts[1:])
    except ValueError:
        return None

    metrics = {}
    if not flags & 1:
        metrics["bme280"] = {"temp_c": t / 100, "humidity": rh / 100, "pressure_hpa": pa / 100}
    if not flags & 2:
        metrics["lm35"] = {"temp_c": lm / 100}
    return {"seq": seq, "epoch": epoch, "time_set": not flags & 4, "metrics": metrics}
