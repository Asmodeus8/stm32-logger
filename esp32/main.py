# ESP32 bridge (MicroPython): reads $LOG frames from the STM32 logger over UART2,
# acknowledges them, publishes them over MQTT, and keeps the STM32's RTC set from NTP.
#
# ESP32 GPIO17 (TX2) -> STM32 PA10, ESP32 GPIO16 (RX2) <- STM32 PA9, common GND.

import json
import time

import machine
import network
import ntptime
from umqtt.simple import MQTTClient

import frames

WIFI_SSID = "your-ssid"
WIFI_PASSWORD = "your-password"
MQTT_BROKER = "192.168.1.10"
DEVICE_ID = "stm32-logger-01"
TIME_SYNC_EVERY_S = 3600

# MicroPython's epoch is 2000-01-01; the STM32 frames use Unix time.
UNIX_OFFSET = 946684800

uart = machine.UART(2, baudrate=115200, tx=17, rx=16, timeout=50)
wdt = machine.WDT(timeout=30000)


def wifi():
    sta = network.WLAN(network.STA_IF)
    sta.active(True)
    if not sta.isconnected():
        sta.connect(WIFI_SSID, WIFI_PASSWORD)
        for _ in range(40):
            if sta.isconnected():
                break
            time.sleep_ms(250)
    return sta.isconnected()


def send_time():
    try:
        ntptime.settime()
    except OSError:
        return False
    uart.write(frames.build("TIME,%d" % (time.time() + UNIX_OFFSET)))
    return True


def mqtt():
    c = MQTTClient(DEVICE_ID, MQTT_BROKER, keepalive=60)
    c.connect()
    return c


def iso(epoch):
    t = time.gmtime(epoch - UNIX_OFFSET)
    return "%04d-%02d-%02dT%02d:%02d:%02dZ" % t[:6]


def run():
    client = None
    last_sync = -TIME_SYNC_EVERY_S
    buf = b""
    while True:
        wdt.feed()
        if not wifi():
            time.sleep(1)
            continue
        if time.time() - last_sync >= TIME_SYNC_EVERY_S and send_time():
            last_sync = time.time()
        if client is None:
            try:
                client = mqtt()
            except OSError:
                time.sleep(1)
                continue

        chunk = uart.read()
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            frame = frames.parse_log(raw.decode("ascii", "ignore"))
            if frame is None:
                continue
            uart.write(frames.build("ACK,%d" % frame["seq"]))
            if not frame["metrics"]:
                continue
            payload = {"metrics": frame["metrics"]}
            if frame["time_set"]:
                payload["ts"] = iso(frame["epoch"])
            try:
                client.publish("devices/%s/telemetry" % DEVICE_ID, json.dumps(payload))
            except OSError:
                client = None


run()
