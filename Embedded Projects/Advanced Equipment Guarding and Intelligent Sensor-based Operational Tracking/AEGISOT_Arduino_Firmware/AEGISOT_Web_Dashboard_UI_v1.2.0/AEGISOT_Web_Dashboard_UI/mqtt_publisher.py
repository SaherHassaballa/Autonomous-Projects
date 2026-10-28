#!/usr/bin/env python3
"""Optional, publish-only MQTT integration for the AEGISOT USB serial bridge.

This module never opens or writes to the ESP32 serial port. It publishes snapshots
provided by the existing AegisotBridge through MQTT. No MQTT subscriptions/commands.
"""
from __future__ import annotations

import json
import socket
import ssl
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _json_safe(value: Any) -> Any:
    """Convert values to strict JSON-safe values (non-finite floats become null)."""
    if isinstance(value, float):
        if value != value or value in (float("inf"), float("-inf")):
            return None
        return value
    if isinstance(value, dict):
        return {str(k): _json_safe(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json_safe(v) for v in value]
    if isinstance(value, (str, int, bool)) or value is None:
        return value
    return str(value)


class MqttPublisher:
    """Background MQTT publisher with optional, configurable per-sensor topics."""

    def __init__(
        self,
        config_path: str | Path,
        state_provider: Callable[[], dict[str, Any]],
        log_callback: Callable[[str, str], None] | None = None,
    ) -> None:
        self.config_path = Path(config_path).expanduser().resolve()
        self.state_provider = state_provider
        self.log_callback = log_callback or (lambda _message, _level="info": None)
        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._client = None
        self._connected = False
        self._last_error = ""
        self._last_error_log = 0.0
        self._last_event_id = 0
        self._started = False
        self._config: dict[str, Any] = {}
        self._topic_map: dict[str, str] = {}
        self._load_error = ""
        try:
            self._config = json.loads(self.config_path.read_text(encoding="utf-8"))
            self._validate_config()
        except Exception as exc:
            self._load_error = str(exc)

    def _validate_config(self) -> None:
        cfg = self._config
        if not isinstance(cfg, dict):
            raise ValueError("MQTT config root must be a JSON object")
        if not cfg.get("enabled", True):
            return
        host = str(cfg.get("host", "")).strip()
        if not host or "YOUR_" in host or host.lower().startswith("replace"):
            raise ValueError("Set your MQTT broker host in the MQTT config file")
        port = int(cfg.get("port", 8883))
        if not 1 <= port <= 65535:
            raise ValueError("MQTT port must be between 1 and 65535")
        base = str(cfg.get("base_topic", "factory/devices/aegisot-node-01")).strip().strip("/")
        if not base or any(part in ("+", "#") for part in base.split("/")):
            raise ValueError("base_topic must be a publishable topic prefix without MQTT wildcards")
        self._config["base_topic"] = base
        device_id = str(cfg.get("device_id", "aegisot-node-01")).strip()
        self._config["device_id"] = device_id or "aegisot-node-01"
        self._config["port"] = port
        self._config["qos"] = max(0, min(1, int(cfg.get("qos", 0))))
        self._config["publish_idle_interval_seconds"] = max(0.5, float(cfg.get("publish_idle_interval_seconds", 1.0)))
        self._config["publish_recording_interval_seconds"] = max(0.2, float(cfg.get("publish_recording_interval_seconds", 0.2)))
        self._config["tls"] = bool(cfg.get("tls", True))
        self._config["retain_status"] = bool(cfg.get("retain_status", True))
        self._config["retain_telemetry"] = bool(cfg.get("retain_telemetry", False))
        self._config["retain_sensor_topics"] = bool(cfg.get("retain_sensor_topics", False))
        self._config["username"] = str(cfg.get("username", ""))
        self._config["password"] = str(cfg.get("password", ""))
        self._config["client_id"] = str(cfg.get("client_id", "")).strip() or f"aegisot-python-{socket.gethostname()}"
        self._topic_map = {
            "telemetry": f"{base}/telemetry",
            "status": f"{base}/status",
            "motor": f"{base}/motor",
            "ina219": f"{base}/sensors/ina219",
            "temperature": f"{base}/sensors/temperature",
            "mpu6050": f"{base}/sensors/mpu6050",
            "encoder": f"{base}/sensors/encoder",
            "recording": f"{base}/recording",
            "events": f"{base}/events",
            "availability": f"{base}/availability",
        }

    def start(self) -> None:
        if self._started:
            return
        self._started = True
        if self._load_error:
            self._log(f"MQTT publishing NOT started: {self._load_error}", "error")
            return
        if not self._config.get("enabled", True):
            self._log("MQTT publishing is disabled in config; serial, CSV, and web dashboard remain unchanged.", "info")
            return
        try:
            import paho.mqtt.client as mqtt
        except ImportError:
            self._log("MQTT package is missing. Install optional MQTT dependencies: python -m pip install -r requirements-mqtt.txt", "error")
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, args=(mqtt,), name="aegisot-mqtt-publisher", daemon=True)
        self._thread.start()
        self._log(f"MQTT publisher enabled. Publishing under {self._config['base_topic']}/# (publish-only).", "info")

    def _run(self, mqtt_module: Any) -> None:
        cfg = self._config
        try:
            try:
                client = mqtt_module.Client(
                    callback_api_version=mqtt_module.CallbackAPIVersion.VERSION2,
                    client_id=cfg["client_id"],
                    protocol=mqtt_module.MQTTv311,
                )
            except (AttributeError, TypeError):
                # Compatibility fallback for paho-mqtt 1.x; requirements-mqtt.txt recommends 2.x.
                client = mqtt_module.Client(client_id=cfg["client_id"], protocol=mqtt_module.MQTTv311)
            self._client = client
            if cfg.get("username"):
                client.username_pw_set(cfg["username"], cfg.get("password", ""))
            base_availability = self._topic_map["availability"]
            will = {"device_id": cfg["device_id"], "status": "offline", "timestamp_utc": None}
            client.will_set(base_availability, json.dumps(will, separators=(",", ":")), qos=1, retain=True)
            if cfg.get("tls", True):
                ca_certs = cfg.get("ca_certs") or None
                client.tls_set(ca_certs=ca_certs, cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS)
                client.tls_insecure_set(False)
            client.on_connect = self._on_connect
            client.on_disconnect = self._on_disconnect
            if hasattr(client, "on_connect_fail"):
                client.on_connect_fail = self._on_connect_fail
            if hasattr(client, "reconnect_delay_set"):
                client.reconnect_delay_set(min_delay=1, max_delay=30)
            client.connect_async(str(cfg["host"]), int(cfg["port"]), keepalive=30)
            client.loop_start()
        except Exception as exc:
            self._set_error(f"MQTT client setup failed: {exc}", log=True)
            return

        next_publish = 0.0
        previous_state: dict[str, Any] = {}
        while not self._stop.is_set():
            try:
                state = self.state_provider()
                recording = bool(state.get("recording"))
                interval = cfg["publish_recording_interval_seconds"] if recording else cfg["publish_idle_interval_seconds"]
                connected = bool(client.is_connected())
                with self._lock:
                    self._connected = connected
                if connected and time.monotonic() >= next_publish:
                    self._publish_snapshot(state)
                    next_publish = time.monotonic() + interval
                previous_state = state
            except Exception as exc:
                self._set_error(f"MQTT publish cycle error: {exc}", log=True)
            self._stop.wait(0.05)

        try:
            if client.is_connected():
                payload = {"device_id": cfg["device_id"], "status": "offline", "timestamp_utc": utc_now()}
                client.publish(self._topic_map["availability"], json.dumps(payload), qos=1, retain=True)
                client.disconnect()
            client.loop_stop()
        except Exception:
            pass
        with self._lock:
            self._connected = False
            self._client = None

    def _on_connect(self, client: Any, userdata: Any, flags: Any, reason_code: Any, properties: Any = None) -> None:
        failed = bool(getattr(reason_code, "is_failure", False))
        try:
            failed = failed or int(reason_code) != 0
        except Exception:
            failed = failed or str(reason_code).lower() not in ("success", "0")
        with self._lock:
            self._connected = not failed
            self._last_error = "" if not failed else f"MQTT connection rejected: {reason_code}"
        if failed:
            self._log(f"MQTT connection rejected: {reason_code}", "error")
            return
        availability = {"device_id": self._config["device_id"], "status": "online", "timestamp_utc": utc_now()}
        self._publish(self._topic_map["availability"], availability, qos=1, retain=True)
        self._log(f"MQTT connected to {self._config['host']}:{self._config['port']} (TLS={self._config['tls']}).", "ok")

    def _on_disconnect(self, client: Any, userdata: Any, disconnect_flags: Any = None, reason_code: Any = None, properties: Any = None) -> None:
        with self._lock:
            was_connected = self._connected
            self._connected = False
        if was_connected and not self._stop.is_set():
            self._set_error(f"MQTT disconnected: {reason_code}", log=True)

    def _on_connect_fail(self, client: Any, userdata: Any) -> None:
        self._set_error("MQTT connection attempt failed; automatic reconnect will continue.", log=True)

    def _publish(self, topic: str, payload: dict[str, Any], qos: int | None = None, retain: bool = False) -> bool:
        client = self._client
        if client is None or not client.is_connected():
            return False
        try:
            info = client.publish(
                topic,
                json.dumps(_json_safe(payload), ensure_ascii=False, separators=(",", ":"), allow_nan=False),
                qos=self._config["qos"] if qos is None else qos,
                retain=retain,
            )
            rc = getattr(info, "rc", 0)
            if rc != 0:
                self._set_error(f"MQTT publish failed for {topic} (rc={rc}).", log=False)
                return False
            return True
        except Exception as exc:
            self._set_error(f"MQTT publish failed for {topic}: {exc}", log=False)
            return False

    def _publish_snapshot(self, state: dict[str, Any]) -> None:
        cfg = self._config
        telem = dict(state.get("telemetry") or {})
        now = utc_now()
        common = {
            "schema_version": 1,
            "device_id": cfg["device_id"],
            "timestamp_utc": now,
            "serial_connected": bool(state.get("serial_connected")),
            "recording": bool(state.get("recording")),
            "active_stage": state.get("active_stage"),
            "pending_stage": state.get("pending_stage"),
            "stage_elapsed_ms": int(state.get("stage_elapsed_ms") or 0),
            "stage_rows": int(state.get("stage_rows") or 0),
            "target_rows": int(state.get("target_rows") or 0),
            "target_hz": int(state.get("target_hz") or 0),
            "total_rows": int(state.get("total_rows") or 0),
            "telemetry_updated_at_unix": state.get("telemetry_updated_at") or None,
            "telemetry": telem,
        }
        status = {
            "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": now,
            "status": "online" if state.get("serial_connected") else "serial_disconnected",
            "serial_connected": bool(state.get("serial_connected")),
            "serial_port": state.get("serial_port"), "serial_error": state.get("serial_error") or None,
            "recording": bool(state.get("recording")), "active_stage": state.get("active_stage"),
            "pending_stage": state.get("pending_stage"), "stage_elapsed_ms": int(state.get("stage_elapsed_ms") or 0),
            "stage_rows": int(state.get("stage_rows") or 0), "total_rows": int(state.get("total_rows") or 0),
            "motor_running": bool(telem.get("motor_running")), "fault": telem.get("fault"),
        }
        motor = {
            "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": now,
            "running": bool(telem.get("motor_running")), "direction": telem.get("direction"),
            "speed_percent": telem.get("speed"), "pwm_duty_8bit": telem.get("pwm"),
            "current_ma": telem.get("current"), "bus_voltage_v": telem.get("voltage"),
            "overcurrent_fault": bool(telem.get("overcurrent")), "last_stop_reason": telem.get("stop_reason"),
            "fault": telem.get("fault"),
        }
        ina = {
            "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": now,
            "valid": bool(telem.get("ina_valid")), "bus_voltage_v": telem.get("voltage"),
            "shunt_voltage_mv": telem.get("shunt_voltage"), "current_ma": telem.get("current"),
            "power_mw": telem.get("power"),
        }
        temperature = {
            "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": now,
            "valid": bool(telem.get("temperature_valid")), "temperature_c": telem.get("temperature"),
        }
        mpu = {
            "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": now,
            "valid": bool(telem.get("mpu_valid")),
            "acceleration_m_s2": {"x": telem.get("ax"), "y": telem.get("ay"), "z": telem.get("az")},
            "gyro_rad_s": {"x": telem.get("gx"), "y": telem.get("gy"), "z": telem.get("gz")},
            "vibration_index_g": telem.get("vibration_index_g"),
        }
        encoder = {
            "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": now,
            "count": telem.get("encoder"), "delta": telem.get("encoder_delta"),
            "counts_per_second": telem.get("cps"), "counts_per_output_revolution": telem.get("counts_per_rev"),
            "rpm": telem.get("rpm"), "rpm_valid": telem.get("rpm") is not None,
        }
        recording = {
            "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": now,
            "active": bool(state.get("recording")), "condition": state.get("active_stage"),
            "pending_condition": state.get("pending_stage"), "elapsed_ms": int(state.get("stage_elapsed_ms") or 0),
            "stage_rows": int(state.get("stage_rows") or 0), "target_rows": int(state.get("target_rows") or 0),
            "target_hz": int(state.get("target_hz") or 0), "counts": state.get("counts", {}),
            "total_rows": int(state.get("total_rows") or 0),
        }
        topics = [
            ("telemetry", common, cfg["retain_telemetry"]),
            ("status", status, cfg["retain_status"]),
            ("motor", motor, cfg["retain_sensor_topics"]),
            ("ina219", ina, cfg["retain_sensor_topics"]),
            ("temperature", temperature, cfg["retain_sensor_topics"]),
            ("mpu6050", mpu, cfg["retain_sensor_topics"]),
            ("encoder", encoder, cfg["retain_sensor_topics"]),
            ("recording", recording, cfg["retain_status"]),
        ]
        for key, payload, retain in topics:
            topic = self._topic_map[key] if key in self._topic_map else f"{cfg['base_topic']}/sensors/{key}"
            self._publish(topic, payload, retain=retain)

        events = state.get("events") or []
        latest_id = self._last_event_id
        for event in events:
            event_id = int(event.get("id", 0))
            if event_id <= self._last_event_id:
                continue
            ok = self._publish(self._topic_map["events"], {
                "schema_version": 1, "device_id": cfg["device_id"], "timestamp_utc": event.get("timestamp_utc") or now,
                "event_id": event_id, "level": event.get("level"), "message": event.get("message"),
            }, retain=False)
            if ok:
                latest_id = max(latest_id, event_id)
            else:
                break
        if latest_id > self._last_event_id:
            self._last_event_id = latest_id

    def status(self) -> dict[str, Any]:
        with self._lock:
            enabled = bool(self._config.get("enabled", True)) and not bool(self._load_error)
            return {
                "enabled": enabled,
                "connected": self._connected,
                "host": self._config.get("host", "") if enabled else "",
                "base_topic": self._config.get("base_topic", "") if enabled else "",
                "last_error": self._last_error or self._load_error,
                "topics": dict(self._topic_map),
            }

    def _log(self, message: str, level: str = "info") -> None:
        try:
            self.log_callback(message, level)
        except Exception:
            pass

    def _set_error(self, message: str, log: bool = True) -> None:
        with self._lock:
            self._last_error = message
            should_log = log and (time.monotonic() - self._last_error_log >= 10.0)
            if should_log:
                self._last_error_log = time.monotonic()
        if should_log:
            self._log(message, "error")

    def shutdown(self) -> None:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.5)
        with self._lock:
            self._connected = False
