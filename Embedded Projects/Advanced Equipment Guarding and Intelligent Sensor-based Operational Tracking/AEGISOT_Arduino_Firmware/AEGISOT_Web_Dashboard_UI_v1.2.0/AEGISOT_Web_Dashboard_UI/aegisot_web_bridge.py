#!/usr/bin/env python3
"""Local AEGISOT web dashboard bridge for the existing USB-Serial test firmware.

Runs a tiny HTTP server and exclusively owns the ESP32 serial port. No MQTT/cloud.
The firmware remains responsible for motor safety and for emitting CSV_DATA at 5 Hz.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import re
import signal
import threading
import time
from collections import deque
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from mqtt_publisher import MqttPublisher

try:
    import serial
except ImportError:
    serial = None

BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = Path.home() / "AEGISOT_Data"
BAUD_DEFAULT = 115200
TARGET_HZ = 5
TARGET_DURATION_SECONDS = 300
TARGET_ROWS = TARGET_HZ * TARGET_DURATION_SECONDS
STAGE_COMMANDS = {
    "normal": (b"N", "Normal"),
    "overheated": (b"O", "Overheated"),
    "vibration_issue": (b"V", "Vibration issue"),
    "rpm_issue": (b"P", "RPM issue"),
}
MOTOR_COMMANDS = {
    "up": b"u", "down": b"d", "stop": b"s", "forward": b"f",
    "reverse": b"r", "zero_encoder": b"0", "clear_fault": b"c",
    "help": b"h",
}
DEVICE_FIELDS = [
    "device_time_ms", "stage_elapsed_ms", "condition", "motor_running",
    "motor_direction", "speed_percent", "pwm_duty_8bit", "ina219_ok",
    "power_data_valid", "bus_voltage_v", "shunt_voltage_mv", "current_ma",
    "power_mw", "ds18b20_ok", "temperature_valid", "temperature_c",
    "mpu6050_ok", "mpu_data_valid", "accel_x_m_s2", "accel_y_m_s2",
    "accel_z_m_s2", "gyro_x_rad_s", "gyro_y_rad_s", "gyro_z_rad_s",
    "encoder_count", "encoder_delta", "encoder_counts_per_second",
    "encoder_counts_per_revolution", "rpm_valid", "rpm", "overcurrent_fault",
    "last_motor_stop_reason",
]
DATA_FIELDS = ["host_received_timestamp_utc"] + DEVICE_FIELDS
SUMMARY_FIELDS = [
    "condition", "started_at_utc", "ended_at_utc", "duration_seconds",
    "target_hz", "target_duration_seconds", "target_readings",
    "collected_readings", "target_reached", "stop_reason", "data_file",
]


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def as_float(value: Any, default: float | None = None) -> float | None:
    try:
        n = float(value)
        return n if math.isfinite(n) else default
    except (TypeError, ValueError):
        return default


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


class AegisotBridge:
    def __init__(self, serial_port: str | None, baud: int, mqtt_config: str | None = None) -> None:
        self.lock = threading.RLock()
        self.serial_port = serial_port
        self.baud = baud
        self.ser = None
        self.serial_connected = False
        self.serial_error = ""
        self.mqtt_publisher: MqttPublisher | None = None
        self.running = True
        self.recording = False
        self.active_stage: str | None = None
        self.pending_stage: str | None = None
        self.last_stage: str | None = None
        self.last_stage_elapsed_ms = 0
        self.stage_started_monotonic: float | None = None
        self.stage_started_utc = ""
        self.stage_rows = 0
        self.total_rows = 0
        self.stage_counts = {key: 0 for key in STAGE_COMMANDS}
        self.stage_elapsed_history = {key: 0.0 for key in STAGE_COMMANDS}
        self.last_device_count = 0
        self.pending_since: float | None = None
        self.data_path: Path | None = None
        self.summary_path: Path | None = None
        self.data_file = None
        self.data_writer = None
        self.summary_file = None
        self.summary_writer = None
        self.telemetry_seq = 0
        self.telemetry_updated_at = 0.0
        self.latest: dict[str, Any] = {
            "motor_running": False, "direction": "forward", "speed": 0, "pwm": 0,
            "voltage": None, "shunt_voltage": None, "current": None, "power": None,
            "temperature": None, "temperature_valid": False,
            "ina_valid": False, "mpu_valid": False,
            "ax": None, "ay": None, "az": None, "gx": None, "gy": None, "gz": None,
            "encoder": 0, "encoder_delta": 0, "cps": None, "rpm": None,
            "counts_per_rev": 0, "fault": "none", "stop_reason": "boot",
            "overcurrent": False, "vibration_index_g": None,
        }
        self.accel_magnitudes: deque[float] = deque(maxlen=50)
        self.events: deque[dict[str, Any]] = deque(maxlen=80)
        self.event_seq = 0
        self.session_tag = datetime.now().strftime("%Y%m%d_%H%M%S")
        self._add_event("Dashboard bridge initialized. Recording is OFF.", "info")
        if serial_port:
            self._open_serial()
        if mqtt_config:
            self.mqtt_publisher = MqttPublisher(mqtt_config, state_provider=self.state, log_callback=self._add_event)
            self.mqtt_publisher.start()

    def _add_event(self, message: str, level: str = "info") -> None:
        with self.lock:
            self.event_seq += 1
            self.events.append({"id": self.event_seq, "time": datetime.now().strftime("%H:%M:%S"),
                                "timestamp_utc": utc_now(), "message": message, "level": level})

    def _open_serial(self) -> None:
        if serial is None:
            self.serial_error = "pyserial is not installed. Run: python3 -m pip install pyserial"
            self._add_event(self.serial_error, "error")
            return
        try:
            self.ser = serial.Serial(self.serial_port, self.baud, timeout=0.2, write_timeout=1)
            # Avoid resetting the board via explicit DTR manipulation. Some USB-serial devices reset on open anyway.
            self.serial_connected = True
            self.serial_error = ""
            self._add_event(f"Connected to ESP32 serial {self.serial_port} at {self.baud} baud.", "ok")
            threading.Thread(target=self._serial_reader, name="aegisot-serial-reader", daemon=True).start()
        except Exception as exc:
            self.ser = None
            self.serial_connected = False
            self.serial_error = str(exc)
            self._add_event(f"Could not open {self.serial_port}: {exc}", "error")
            self._add_event("Close Arduino Serial Monitor and keyboard_motor_test.py; only one process can own the serial port.", "warn")

    def _write_serial(self, data: bytes) -> None:
        with self.lock:
            if not self.serial_connected or self.ser is None:
                raise RuntimeError("ESP32 serial port is not connected")
            self.ser.write(data)
            self.ser.flush()

    def start_stage(self, stage: str) -> dict[str, Any]:
        if stage not in STAGE_COMMANDS:
            raise ValueError("Unknown stage")
        with self.lock:
            if not self.serial_connected:
                raise RuntimeError("ESP32 is not connected over USB Serial")
            if self.recording or self.pending_stage:
                raise RuntimeError("Stop or finish the current stage request before starting another")
            command, label = STAGE_COMMANDS[stage]
            self.pending_stage = stage
            self.pending_since = time.monotonic()
        try:
            self._write_serial(command)
        except Exception:
            with self.lock:
                self.pending_stage = None
                self.pending_since = None
            raise
        self._add_event(f"Start requested for {label}; waiting for ESP32 LOG_STARTED acknowledgment.", "warn")
        return {"ok": True, "message": f"Start request sent for {label}; waiting for device acknowledgment."}

    def stop_stage(self) -> dict[str, Any]:
        with self.lock:
            if not self.serial_connected:
                raise RuntimeError("ESP32 is not connected over USB Serial")
            if not self.recording:
                raise RuntimeError("No active recording stage")
        self._write_serial(b"X")
        self._add_event("Stop requested. Waiting for ESP32 LOG_STOPPED acknowledgment.", "warn")
        return {"ok": True, "message": "Stop request sent; waiting for device acknowledgment."}

    def motor_command(self, command: str) -> dict[str, Any]:
        if command not in MOTOR_COMMANDS:
            raise ValueError("Unsupported motor command")
        self._write_serial(MOTOR_COMMANDS[command])
        labels = {"up":"Increase speed by 5%", "down":"Decrease speed by 5%", "stop":"STOP motor",
                  "forward":"Select forward (firmware stops before direction change)",
                  "reverse":"Select reverse (firmware stops before direction change)",
                  "zero_encoder":"Zero encoder (firmware allows only while stopped)",
                  "clear_fault":"Clear overcurrent fault (firmware safety checks apply)", "help":"Print firmware help"}
        self._add_event(f"Motor command sent: {labels[command]}.", "warn" if command == "stop" else "info")
        return {"ok": True, "message": f"Command sent: {labels[command]}"}

    def _ensure_files(self) -> None:
        if self.data_writer is not None:
            return
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        tag = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.data_path = DATA_DIR / f"aegisot_web_dataset_{tag}.csv"
        self.summary_path = DATA_DIR / f"aegisot_web_summary_{tag}.csv"
        self.data_file = self.data_path.open("w", newline="", encoding="utf-8")
        self.data_writer = csv.DictWriter(self.data_file, fieldnames=DATA_FIELDS)
        self.data_writer.writeheader()
        self.data_file.flush()
        self.summary_file = self.summary_path.open("w", newline="", encoding="utf-8")
        self.summary_writer = csv.DictWriter(self.summary_file, fieldnames=SUMMARY_FIELDS)
        self.summary_writer.writeheader()
        self.summary_file.flush()
        self._add_event(f"CSV files created: {self.data_path.name} and {self.summary_path.name}.", "ok")

    def _on_serial_line(self, line: str) -> None:
        line = line.strip()
        if not line:
            return
        if line.startswith("CSV_DATA,"):
            self._handle_csv_data(line)
        elif line.startswith("LOG_STARTED,"):
            self._handle_log_started(line)
        elif line.startswith("LOG_STOPPED,"):
            self._handle_log_stopped(line)
        elif line.startswith("LOG_REJECTED,"):
            parts = line.split(",")
            condition = parts[1] if len(parts) > 1 else "unknown"
            reason = parts[2] if len(parts) > 2 else "unspecified"
            with self.lock:
                self.pending_stage = None
                self.pending_since = None
            self._add_event(f"ESP32 rejected recording request ({condition}): {reason}.", "error")
        elif line == "LOG_NOT_ACTIVE":
            self._add_event("ESP32 reports that no recording is active.", "warn")
        elif line.startswith("LOG_TARGET_REACHED,"):
            parts = line.split(",")
            if len(parts) >= 4:
                self._add_event(f"Target reached for {parts[1]}: {parts[2]} rows in {as_int(parts[3])/1000:.1f}s. Recording remains active until stopped.", "ok")
        else:
            self._parse_telemetry_line(line)
            # Retain noteworthy firmware messages without flooding the activity log every telemetry line.
            if any(token in line for token in ("MOTOR STOP", "MOTOR COMMAND", "SAFETY TRIP", "ERROR:", "refused", "NOT FOUND", "FATAL:", "Connected", "Ready.")):
                level = "error" if any(x in line for x in ("SAFETY TRIP", "ERROR:", "refused", "FATAL:")) else "info"
                self._add_event(line[:220], level)

    def _handle_log_started(self, line: str) -> None:
        parts = line.split(",")
        if len(parts) < 5:
            self._add_event(f"Malformed LOG_STARTED acknowledgment: {line}", "error")
            return
        condition = parts[1]
        if condition not in STAGE_COMMANDS:
            self._add_event(f"Unknown stage from firmware: {condition}", "error")
            return
        try:
            self._ensure_files()
        except Exception as exc:
            self._add_event(f"Could not create CSV output files: {exc}. Sending recording STOP for safety.", "error")
            try:
                self._write_serial(b"X")
            except Exception:
                pass
            with self.lock:
                self.pending_stage = None
                self.pending_since = None
            return
        with self.lock:
            self.recording = True
            self.active_stage = condition
            self.last_stage = condition
            self.pending_stage = None
            self.pending_since = None
            self.stage_started_monotonic = time.monotonic()
            self.stage_started_utc = utc_now()
            self.stage_rows = 0
            self.stage_counts[condition] = 0
            self.last_stage_elapsed_ms = 0
            self.last_device_count = 0
        self._add_event(f"RECORDING ON — {STAGE_COMMANDS[condition][1]}: {TARGET_DURATION_SECONDS//60} min, {TARGET_HZ} Hz, target {TARGET_ROWS} rows.", "ok")

    def _handle_log_stopped(self, line: str) -> None:
        parts = line.split(",")
        if len(parts) < 5:
            self._add_event(f"Malformed LOG_STOPPED acknowledgment: {line}", "error")
            return
        condition = parts[1]
        elapsed_ms = as_int(parts[2])
        device_count = as_int(parts[3])
        reason = ",".join(parts[4:])
        with self.lock:
            matching = self.recording and self.active_stage == condition
            saved_rows = self.stage_rows if matching else 0
            started_utc = self.stage_started_utc
            path_name = str(self.data_path or "")
            self.recording = False
            self.active_stage = None
            self.pending_stage = None
            self.pending_since = None
            self.last_stage = condition if condition in STAGE_COMMANDS else self.last_stage
            self.last_stage_elapsed_ms = elapsed_ms
            self.stage_elapsed_history[condition] = elapsed_ms / 1000.0 if condition in self.stage_elapsed_history else 0.0
            self.stage_started_monotonic = None
            self.stage_rows = 0
            self.last_device_count = device_count
            try:
                if self.summary_writer is not None and condition in STAGE_COMMANDS:
                    self.summary_writer.writerow({
                        "condition": condition, "started_at_utc": started_utc, "ended_at_utc": utc_now(),
                        "duration_seconds": f"{elapsed_ms/1000:.3f}", "target_hz": TARGET_HZ,
                        "target_duration_seconds": TARGET_DURATION_SECONDS, "target_readings": TARGET_ROWS,
                        "collected_readings": saved_rows,
                        "target_reached": "yes" if saved_rows >= TARGET_ROWS else "no",
                        "stop_reason": reason, "data_file": path_name,
                    })
                    self.summary_file.flush()
            except Exception as exc:
                self._add_event(f"Could not update CSV summary: {exc}", "error")
        self._add_event(f"RECORDING OFF — {condition}: saved {saved_rows} rows in {elapsed_ms/1000:.1f}s; target {TARGET_ROWS}; reason={reason}.", "ok" if saved_rows >= TARGET_ROWS else "warn")

    def _handle_csv_data(self, line: str) -> None:
        values = line[len("CSV_DATA,"):].split(",")
        if len(values) != len(DEVICE_FIELDS):
            self._add_event(f"Discarded malformed CSV_DATA row: {len(values)} fields; expected {len(DEVICE_FIELDS)}.", "error")
            return
        raw = dict(zip(DEVICE_FIELDS, values))
        with self.lock:
            if not self.recording or self.data_writer is None:
                # The device only emits CSV_DATA during logging; avoid silently claiming this row was saved.
                self._add_event("CSV_DATA arrived but host CSV is not ready; row not saved.", "error")
                return
            try:
                row = {"host_received_timestamp_utc": utc_now(), **raw}
                self.data_writer.writerow(row)
                self.data_file.flush()
            except Exception as exc:
                self._add_event(f"Failed writing a CSV row: {exc}", "error")
                return
            self.stage_rows += 1
            self.total_rows += 1
            self.stage_counts[self.active_stage or raw["condition"]] = self.stage_rows
            self.last_device_count += 1
            self.last_stage_elapsed_ms = as_int(raw.get("stage_elapsed_ms"), self.last_stage_elapsed_ms)
            self._apply_csv_telemetry(raw)
            if self.stage_rows == TARGET_ROWS:
                self._add_event(f"Target reached: {self.active_stage} has {TARGET_ROWS} saved rows. Recording will continue until you stop it.", "ok")

    def _apply_csv_telemetry(self, row: dict[str, str]) -> None:
        def f(k: str) -> float | None: return as_float(row.get(k))
        def b(k: str) -> bool: return row.get(k, "0").strip() == "1"
        self.latest.update({
            "motor_running": b("motor_running"), "direction": row.get("motor_direction", "forward"),
            "speed": as_int(row.get("speed_percent")), "pwm": as_int(row.get("pwm_duty_8bit")),
            "ina_valid": b("power_data_valid"), "voltage": f("bus_voltage_v"),
            "shunt_voltage": f("shunt_voltage_mv"), "current": f("current_ma"), "power": f("power_mw"),
            "temperature_valid": b("temperature_valid"), "temperature": f("temperature_c"),
            "mpu_valid": b("mpu_data_valid"), "ax": f("accel_x_m_s2"), "ay": f("accel_y_m_s2"),
            "az": f("accel_z_m_s2"), "gx": f("gyro_x_rad_s"), "gy": f("gyro_y_rad_s"), "gz": f("gyro_z_rad_s"),
            "encoder": as_int(row.get("encoder_count")), "encoder_delta": as_int(row.get("encoder_delta")),
            "cps": f("encoder_counts_per_second"), "rpm": f("rpm") if b("rpm_valid") else None,
            "counts_per_rev": as_int(row.get("encoder_counts_per_revolution")),
            "overcurrent": b("overcurrent_fault"), "stop_reason": row.get("last_motor_stop_reason", "none"),
            "fault": "overcurrent_fault" if b("overcurrent_fault") else row.get("last_motor_stop_reason", "none"),
        })
        self._update_vibration()
        self._mark_telemetry_updated()

    def _parse_telemetry_line(self, line: str) -> None:
        changed = False
        motor = re.search(r"MOTOR: state=(\S+) direction=(\S+) speed=(\d+)% duty=(\d+)/255", line)
        if motor:
            self.latest.update({"motor_running": motor.group(1) != "STOPPED", "direction": motor.group(2),
                                "speed": int(motor.group(3)), "pwm": int(motor.group(4))})
            changed = True
        if line.startswith("MOTOR STOP | reason="):
            self.latest["stop_reason"] = line.split("reason=", 1)[1]
            if self.latest["stop_reason"] in ("overcurrent_trip", "ina219_read_invalid", "pwm_not_ready"):
                self.latest["fault"] = self.latest["stop_reason"]
            if self.latest["stop_reason"] == "keyboard_stop":
                self.latest["fault"] = "none"
            changed = True
        if line.startswith("FAULTS:"):
            oc = re.search(r"overcurrent=(true|false)", line)
            if oc:
                self.latest["overcurrent"] = oc.group(1) == "true"
                if not self.latest["overcurrent"] and self.latest["fault"] == "overcurrent_fault":
                    self.latest["fault"] = "none"
                changed = True
        if line.startswith("INA219:"):
            m = re.search(r"ok=true bus=([\d.+-]+) V shunt=([\d.+-]+) mV current=([\d.+-]+) mA power=([\d.+-]+) mW", line)
            if m:
                self.latest.update({"ina_valid": True, "voltage": float(m.group(1)), "shunt_voltage": float(m.group(2)),
                                    "current": float(m.group(3)), "power": float(m.group(4))})
            else:
                self.latest["ina_valid"] = "ok=true" in line and "data_valid=false" not in line
                if not self.latest["ina_valid"]:
                    self.latest.update({"voltage": None, "shunt_voltage": None, "current": None, "power": None})
            changed = True
        if line.startswith("DS18B20:"):
            m = re.search(r"ok=true temp=([\d.+-]+) C", line)
            self.latest["temperature_valid"] = bool(m)
            self.latest["temperature"] = float(m.group(1)) if m else None
            changed = True
        if line.startswith("MPU6050:"):
            m = re.search(r"ok=true accel=\[([\d.+-]+),\s*([\d.+-]+),\s*([\d.+-]+)\] m/s\^2 gyro=\[([\d.+-]+),\s*([\d.+-]+),\s*([\d.+-]+)\] rad/s", line)
            self.latest["mpu_valid"] = bool(m)
            if m:
                self.latest.update(dict(zip(("ax", "ay", "az", "gx", "gy", "gz"), map(float, m.groups()))))
                self._update_vibration()
            else:
                self.latest.update({"ax": None, "ay": None, "az": None, "gx": None, "gy": None, "gz": None, "vibration_index_g": None})
            changed = True
        if line.startswith("ENCODER:"):
            count = re.search(r"count=(-?\d+)", line)
            delta = re.search(r"delta=(-?\d+)", line)
            cps = re.search(r"cps=([\d.+-]+)", line)
            cpr = re.search(r"counts_per_rev=(-?\d+)", line)
            rpm = re.search(r"rpm=([\d.+-]+)", line)
            self.latest.update({
                "encoder": int(count.group(1)) if count else self.latest["encoder"],
                "encoder_delta": int(delta.group(1)) if delta else self.latest["encoder_delta"],
                "cps": float(cps.group(1)) if cps else self.latest["cps"],
                "counts_per_rev": int(cpr.group(1)) if cpr else self.latest["counts_per_rev"],
                "rpm": float(rpm.group(1)) if rpm else None,
            })
            changed = True
        if changed:
            self._mark_telemetry_updated()

    def _update_vibration(self) -> None:
        vals = [self.latest.get(k) for k in ("ax", "ay", "az")]
        if any(v is None for v in vals):
            self.latest["vibration_index_g"] = None
            return
        magnitude = math.sqrt(sum(float(v) ** 2 for v in vals))
        self.accel_magnitudes.append(magnitude)
        mean_magnitude = sum(self.accel_magnitudes) / len(self.accel_magnitudes)
        variance = sum((v - mean_magnitude) ** 2 for v in self.accel_magnitudes) / len(self.accel_magnitudes)
        self.latest["vibration_index_g"] = math.sqrt(variance) / 9.80665

    def _mark_telemetry_updated(self) -> None:
        self.telemetry_seq += 1
        self.telemetry_updated_at = time.time()

    def _serial_reader(self) -> None:
        while self.running and self.ser is not None:
            try:
                raw = self.ser.readline()
                if not raw:
                    with self.lock:
                        pending = self.pending_stage
                        since = self.pending_since
                    if pending and since and (time.monotonic() - since) > 5.0:
                        with self.lock:
                            if self.pending_stage == pending:
                                self.pending_stage = None
                                self.pending_since = None
                        self._add_event(f"No LOG_STARTED acknowledgment for {pending} after 5 seconds. Check firmware serial logs.", "error")
                    continue
                line = raw.decode("utf-8", errors="replace")
                self._on_serial_line(line)
            except Exception as exc:
                if self.running:
                    with self.lock:
                        self.serial_connected = False
                        self.serial_error = str(exc)
                    self._add_event(f"Serial reader stopped: {exc}", "error")
                break

    def state(self) -> dict[str, Any]:
        mqtt_state = self.mqtt_publisher.status() if self.mqtt_publisher else {"enabled": False, "connected": False, "base_topic": "", "topics": {}, "last_error": ""}
        with self.lock:
            elapsed = self.last_stage_elapsed_ms
            if self.recording and self.stage_started_monotonic is not None:
                elapsed = int((time.monotonic() - self.stage_started_monotonic) * 1000)
            events = list(self.events)[-25:]
            telemetry = dict(self.latest)
            # Avoid emitting NaN/Infinity in JSON.
            for k, v in list(telemetry.items()):
                if isinstance(v, float) and not math.isfinite(v): telemetry[k] = None
            return {
                "ok": True, "serial_connected": self.serial_connected, "serial_port": self.serial_port,
                "baud": self.baud, "serial_error": self.serial_error, "recording": self.recording,
                "active_stage": self.active_stage, "pending_stage": self.pending_stage,
                "display_stage": self.active_stage or self.pending_stage or self.last_stage,
                "stage_elapsed_ms": max(0, elapsed), "stage_rows": self.stage_rows,
                "target_rows": TARGET_ROWS, "target_hz": TARGET_HZ,
                "counts": dict(self.stage_counts), "total_rows": self.total_rows,
                "last_device_count": self.last_device_count, "data_file": str(self.data_path) if self.data_path else "",
                "summary_file": str(self.summary_path) if self.summary_path else "",
                "data_file_name": self.data_path.name if self.data_path else "",
                "summary_file_name": self.summary_path.name if self.summary_path else "",
                "telemetry": telemetry, "telemetry_seq": self.telemetry_seq,
                "telemetry_updated_at": self.telemetry_updated_at,
                "events": events, "server_time": time.time(), "mqtt": mqtt_state,
            }

    def export_csv(self) -> tuple[bytes, str] | None:
        with self.lock:
            if self.data_file is not None:
                self.data_file.flush()
            path = self.data_path
            if path is None or not path.exists():
                return None
            return path.read_bytes(), path.name

    def shutdown(self) -> None:
        # Let the serial reader process LOG_STOPPED before terminating, then stop the motor for safe application exit.
        try:
            if self.serial_connected and self.ser:
                with self.lock:
                    was_recording = self.recording
                if was_recording:
                    self.ser.write(b"X")
                    self.ser.flush()
                    deadline = time.monotonic() + 1.0
                    while time.monotonic() < deadline:
                        with self.lock:
                            if not self.recording:
                                break
                        time.sleep(0.02)
                self.ser.write(b"s")
                self.ser.flush()
                time.sleep(0.1)
        except Exception:
            pass
        if self.mqtt_publisher is not None:
            self.mqtt_publisher.shutdown()
        self.running = False
        with self.lock:
            try:
                if self.data_file:
                    self.data_file.flush(); self.data_file.close()
                if self.summary_file:
                    self.summary_file.flush(); self.summary_file.close()
                if self.ser:
                    self.ser.close()
            except Exception:
                pass
            self.serial_connected = False
        self._add_event("Bridge shut down. Stop command sent if serial was available.", "warn")


class Handler(BaseHTTPRequestHandler):
    bridge: AegisotBridge

    def log_message(self, fmt: str, *args: Any) -> None:
        # Keep server output compact; errors and actions are visible in dashboard log.
        return

    def _send(self, status: int, body: bytes, content_type: str = "application/json; charset=utf-8", extra: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        if extra:
            for k, v in extra.items(): self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _json(self, status: int, obj: dict[str, Any]) -> None:
        self._send(status, json.dumps(obj, ensure_ascii=False, allow_nan=False).encode("utf-8"))

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/state":
            self._json(200, self.bridge.state()); return
        if path == "/api/export":
            exported = self.bridge.export_csv()
            if not exported:
                self._json(404, {"ok": False, "error": "No CSV session has been created yet. Start and stop a recording stage first."}); return
            data, name = exported
            self._send(200, data, "text/csv; charset=utf-8", {"Content-Disposition": f'attachment; filename="{name}"'}); return
        if path in ("/", "/index.html"):
            file_path = BASE_DIR / "index.html"
            self._send(200, file_path.read_bytes(), "text/html; charset=utf-8"); return
        if path == "/favicon.ico":
            self._send(204, b"", "image/x-icon"); return
        self._json(404, {"ok": False, "error": "Not found"})

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length > 16_384: self._json(413, {"ok": False, "error": "Request too large"}); return
            payload = json.loads(self.rfile.read(length) or b"{}")
            if path == "/api/stage/start":
                result = self.bridge.start_stage(str(payload.get("stage", "")))
            elif path == "/api/stage/stop":
                result = self.bridge.stop_stage()
            elif path == "/api/motor/command":
                result = self.bridge.motor_command(str(payload.get("command", "")))
            elif path == "/api/ui/log":
                # Optional UI event, sanitized as a plain string.
                msg = str(payload.get("message", ""))[:180]
                if msg: self.bridge._add_event(msg, "info")
                result = {"ok": True}
            else:
                self._json(404, {"ok": False, "error": "Unknown endpoint"}); return
            self._json(200, result)
        except ValueError as exc:
            self._json(400, {"ok": False, "error": str(exc)})
        except RuntimeError as exc:
            self._json(409, {"ok": False, "error": str(exc)})
        except Exception as exc:
            self._json(500, {"ok": False, "error": f"Internal error: {exc}"})


def main() -> int:
    parser = argparse.ArgumentParser(description="AEGISOT local web dashboard + USB serial bridge with optional MQTT publishing")
    parser.add_argument("--serial-port", default="/dev/ttyUSB0", help="ESP32 serial device (e.g. /dev/ttyUSB0 or /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=BAUD_DEFAULT)
    parser.add_argument("--http-host", default="127.0.0.1", help="Use 127.0.0.1 for local-only access")
    parser.add_argument("--http-port", type=int, default=8765)
    parser.add_argument("--mqtt-config", default=None, help="Optional JSON config to enable outbound MQTT telemetry publishing")
    parser.add_argument("--no-serial", action="store_true", help="Serve UI without opening a serial port (no real data or recording)")
    args = parser.parse_args()
    bridge = AegisotBridge(None if args.no_serial else args.serial_port, args.baud, mqtt_config=args.mqtt_config)
    handler = type("AegisotHandler", (Handler,), {"bridge": bridge})
    try:
        server = ThreadingHTTPServer((args.http_host, args.http_port), handler)
    except OSError as exc:
        bridge.shutdown()
        print(f"Cannot start web server on {args.http_host}:{args.http_port}: {exc}")
        return 1
    print("\nAEGISOT Web Dashboard")
    print(f"  Open: http://{args.http_host}:{args.http_port}")
    print(f"  Serial: {args.serial_port if not args.no_serial else 'disabled'} @ {args.baud}")
    print(f"  CSV output: {DATA_DIR}")
    print(f"  MQTT publishing: {args.mqtt_config if args.mqtt_config else 'disabled (existing behavior)'}")
    print("  STOP recording: dashboard button; motor state is unchanged.")
    print("  Ctrl+C closes the bridge and sends a motor STOP for safety.\n")
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        print("\nShutting down AEGISOT bridge...")
    finally:
        server.shutdown()
        server.server_close()
        bridge.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
