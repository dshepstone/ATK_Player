import json
import socket
import time
from pathlib import Path

from .errors import AtkCommandError, AtkConnectionError, AtkProtocolError, AtkTimeoutError


class AtkPlayer:
    DEFAULT_PORT = 45571
    MAX_RESPONSE_BYTES = 256 * 1024

    def __init__(self, host="127.0.0.1", port=DEFAULT_PORT,
                 connect_timeout=1.5, request_timeout=5.0):
        if host not in ("127.0.0.1", "localhost", "::1"):
            raise ValueError("ATK Player client only permits loopback hosts")
        self.host = host
        self.port = int(port)
        self.connect_timeout = float(connect_timeout)
        self.request_timeout = float(request_timeout)
        self._socket = None
        self._buffer = bytearray()
        self._next_id = 1

    def connect(self):
        if self._socket is not None:
            return self
        try:
            self._socket = socket.create_connection(
                (self.host, self.port), timeout=self.connect_timeout)
            self._socket.settimeout(self.request_timeout)
        except socket.timeout as exc:
            raise AtkTimeoutError("timed out connecting to ATK Player") from exc
        except OSError as exc:
            raise AtkConnectionError("ATK Player is not running or its Local API is disabled") from exc
        return self

    def close(self):
        if self._socket is not None:
            try:
                self._socket.close()
            finally:
                self._socket = None
                self._buffer.clear()

    def __enter__(self):
        return self.connect()

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def _read_line(self):
        while b"\n" not in self._buffer:
            try:
                chunk = self._socket.recv(4096)
            except socket.timeout as exc:
                raise AtkTimeoutError("timed out waiting for ATK Player") from exc
            except OSError as exc:
                raise AtkConnectionError("connection to ATK Player failed") from exc
            if not chunk:
                raise AtkConnectionError("ATK Player closed the connection")
            self._buffer.extend(chunk)
            if len(self._buffer) > self.MAX_RESPONSE_BYTES:
                self.close()
                raise AtkProtocolError("ATK Player response exceeded the safety limit")
        line, _, remainder = self._buffer.partition(b"\n")
        self._buffer[:] = remainder
        return bytes(line)

    def request(self, command, **params):
        self.connect()
        request_id = self._next_id
        self._next_id += 1
        payload = {"id": request_id, "command": command}
        if params:
            payload["params"] = params
        try:
            encoded = json.dumps(payload, ensure_ascii=False,
                                 separators=(",", ":")).encode("utf-8") + b"\n"
            self._socket.sendall(encoded)
            response = json.loads(self._read_line().decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise AtkProtocolError("ATK Player returned invalid JSON") from exc
        except socket.timeout as exc:
            raise AtkTimeoutError("ATK Player request timed out") from exc
        except OSError as exc:
            raise AtkConnectionError("connection to ATK Player failed") from exc
        if not isinstance(response, dict) or response.get("id") != request_id:
            raise AtkProtocolError("ATK Player returned a mismatched request id")
        if response.get("ok") is not True:
            raise AtkCommandError(str(response.get("error", "command failed")))
        result = response.get("result", {})
        if not isinstance(result, dict):
            raise AtkProtocolError("ATK Player result must be an object")
        return result

    def api_info(self): return self.request("get_api_info")
    def list_commands(self): return self.request("list_commands")["commands"]
    def status(self): return self.request("get_status")
    def play(self): return self.request("play")
    def pause(self): return self.request("pause")
    def stop(self): return self.request("stop")
    def seek_frame(self, frame): return self.request("seek_frame", frame=int(frame))
    def step_forward(self): return self.request("step_forward")
    def step_backward(self): return self.request("step_backward")
    def set_loop_enabled(self, enabled): return self.request("set_loop_enabled", enabled=bool(enabled))
    def set_review_range(self, start, end): return self.request("set_loop_range", start=int(start), end=int(end))
    def clear_review_range(self): return self.request("clear_loop_range")
    def add_bookmark(self, frame=None, name="", note="", color=None):
        params = {"name": name, "note": note}
        if frame is not None: params["frame"] = int(frame)
        if color is not None: params["color"] = int(color)
        return self.request("add_bookmark", **params)
    def open_media(self, path, discard_unsaved=False):
        return self.request("open_media", path=str(Path(path).absolute()), discardUnsaved=discard_unsaved)
    def open_project(self, path, discard_unsaved=False):
        return self.request("open_project", path=str(Path(path).absolute()), discardUnsaved=discard_unsaved)
    def new_project(self, discard_unsaved=False):
        return self.request("new_project", discardUnsaved=discard_unsaved)
    def save_project(self): return self.request("save_project")
    def save_project_as(self, path):
        return self.request("save_project_as", path=str(Path(path).absolute()))
    def add_media(self, *paths):
        return self.request("add_media", paths=[str(Path(path).absolute()) for path in paths])
    def list_sources(self): return self.request("list_sources")["sources"]
    def activate_source(self, source_id=None, index=None):
        params = {"sourceId": source_id} if source_id is not None else {"index": int(index)}
        return self.request("activate_source", **params)
    def set_comparison_enabled(self, enabled): return self.request("set_comparison_enabled", enabled=bool(enabled))
    def set_compare_a(self, source_id): return self.request("load_compare_a", sourceId=source_id)
    def set_compare_b(self, source_id): return self.request("load_compare_b", sourceId=source_id)
    def set_compare_offset(self, frame_offset): return self.request("set_compare_offset", slot="b", frameOffset=int(frame_offset))
    def set_compare_view(self, mode, **options): return self.request("set_compare_view", mode=mode, **options)
    def set_compare_audio_mode(self, mode): return self.request("set_compare_audio_mode", mode=mode)
    def load_external_audio(self, path): return self.request("load_external_audio", path=str(Path(path).absolute()))
    def clear_external_audio(self): return self.request("clear_external_audio")
    def set_external_audio_offset(self, frame_offset): return self.request("set_external_audio_offset", frameOffset=int(frame_offset))
    def export_review(self, path, overwrite=False): return self.request("export_review", path=str(Path(path).absolute()), overwrite=overwrite)
    def export_status(self, job_id=None): return self.request("get_export_status", **({"jobId": job_id} if job_id else {}))
    def cancel_export(self, job_id=None): return self.request("cancel_export", **({"jobId": job_id} if job_id else {}))
    def list_bookmarks(self): return self.request("list_bookmarks")["bookmarks"]
    def show_window(self): return self.request("show_window")

    def wait_until_loaded(self, path=None, timeout=20.0, poll_interval=0.05):
        expected = str(Path(path).absolute()) if path is not None else None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            status = self.status()
            actual = status.get("path")
            same = expected is None or (actual and Path(actual) == Path(expected))
            if same and status.get("hasMedia") and status.get("state") not in ("loading", "error"):
                return status
            time.sleep(poll_interval)
        raise AtkTimeoutError("timed out waiting for requested media to load")

    def wait_until_frame(self, frame, timeout=10.0, poll_interval=0.05):
        """Wait until the authoritative zero-based presented frame settles."""
        target = int(frame)
        deadline = time.monotonic() + float(timeout)
        while time.monotonic() < deadline:
            status = self.status()
            if status.get("currentFrame") == target:
                return status
            time.sleep(float(poll_interval))
        raise AtkTimeoutError("timed out waiting for ATK Player frame {}".format(target))
