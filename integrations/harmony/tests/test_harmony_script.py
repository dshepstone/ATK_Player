import json
import re
import unittest
from pathlib import Path


SCRIPT = (Path(__file__).parents[1] / "scripts" / "ATK_Review.js").read_text(encoding="utf-8")


def harmony_to_atk(frame, start):
    return int(frame) - int(start)


def atk_to_harmony(frame, start):
    return int(start) + int(frame)


def serialize_harmony_request(request_id, command, params=None):
    return json.dumps({
        "id": request_id,
        "command": command,
        "params": params or {},
    }, separators=(",", ":"))


def parse_response(raw, request_id=1):
    if "\n" not in raw:
        raise ValueError("incomplete")
    response = json.loads(raw.split("\n", 1)[0])
    if response.get("id") != request_id or response.get("ok") is not True:
        raise ValueError(response.get("error", "invalid response"))
    return response.get("result", {})


def diagnose_response(command, raw, request_id):
    line_count = len([line for line in raw.split("\n") if line])
    escaped = raw.replace("\\", "\\\\").replace("\r", "\\r").replace("\n", "\\n")
    if "\n" not in raw:
        raise ValueError("incomplete")
    try:
        response = json.loads(raw.split("\n", 1)[0])
    except json.JSONDecodeError as error:
        raise ValueError(
            f"malformed JSON for {command} (expected id {request_id}); "
            f"response lines={line_count}; raw={escaped}") from error
    actual = response.get("id")
    actual_label = "missing" if actual is None else str(actual)
    diagnostic = (f"command={command}, expected id={request_id}, actual id={actual_label}, "
                  f"response lines={line_count}, raw={escaped}")
    if actual != request_id:
        raise ValueError(
            f"mismatched response for {command} (expected id {request_id}, "
            f"received id {actual_label}); {diagnostic}")
    return response, diagnostic


def verify_info(info):
    if (info.get("application"), info.get("protocolVersion"), info.get("frameIndexBase")) != (
            "ATK Player", 1, 0):
        raise ValueError("incompatible")


def model_review_workflow(transport, movie, current, start, end):
    verify_info(transport.request("get_api_info", {}))
    transport.request("open_media", {"path": movie, "discardUnsaved": True})
    transport.request("get_status", {})
    transport.request("set_loop_range", {"start": 0, "end": end - start})
    transport.request("set_loop_enabled", {"enabled": True})
    target = harmony_to_atk(current, start)
    transport.request("seek_frame", {"frame": target})
    transport.request("get_status", {})
    transport.request("show_window", {})


def ensure_review_directory(directory_factory, path):
    directory = directory_factory(path)
    if not directory.exists:
        directory.mkdirs()
        directory = directory_factory(path)
        if not directory.exists:
            raise RuntimeError("Could not create the ATK Harmony review folder.")


def validate_exported_movie(file_factory, path):
    exported_movie = file_factory(path)
    if not exported_movie.exists:
        raise RuntimeError("Harmony did not create the review movie.")


def remove_previous_movie(file_factory, path):
    previous = file_factory(path)
    if previous.exists:
        previous.remove()


class HarmonyScriptTests(unittest.TestCase):
    def test_sequential_request_diagnostics_and_id_validation(self):
        requests = [serialize_harmony_request(request_id, command) for request_id, command in (
            (1, "get_api_info"), (2, "get_status"), (3, "get_api_info"))]
        self.assertEqual([json.loads(request)["id"] for request in requests], [1, 2, 3])

        first, first_diagnostic = diagnose_response(
            "get_api_info", '{"id":1,"ok":true,"result":{}}\n', 1)
        second, second_diagnostic = diagnose_response(
            "get_status", '{"id":2,"ok":true,"result":{}}\n', 2)
        third, third_diagnostic = diagnose_response(
            "get_api_info", '{"id":3,"ok":true,"result":{}}\n', 3)
        self.assertEqual((first["id"], second["id"], third["id"]), (1, 2, 3))
        self.assertIn("command=get_api_info, expected id=1, actual id=1", first_diagnostic)
        self.assertIn("command=get_status, expected id=2, actual id=2", second_diagnostic)
        self.assertIn("command=get_api_info, expected id=3, actual id=3", third_diagnostic)

        with self.assertRaisesRegex(
                ValueError, r"mismatched response for get_status \(expected id 2, received id 1\)"):
            diagnose_response("get_status", '{"id":1,"ok":true}\n', 2)
        for response in ('{"ok":true}\n', '{"id":null,"ok":true}\n'):
            with self.assertRaisesRegex(ValueError, r"received id missing"):
                diagnose_response("get_status", response, 2)

        _, multiline_diagnostic = diagnose_response(
            "get_status", '{"id":2,"ok":true}\n{"id":1,"ok":true}\n', 2)
        self.assertIn("response lines=2", multiline_diagnostic)
        self.assertIn(r'raw={"id":2,"ok":true}\n{"id":1,"ok":true}\n', multiline_diagnostic)
        with self.assertRaisesRegex(ValueError, r"malformed JSON for get_status \(expected id 2\)"):
            diagnose_response("get_status", "not-json\n", 2)

        self.assertIn("function ATK_TestSequentialRequests()", SCRIPT)
        sequential_body = SCRIPT.split("function ATK_TestSequentialRequests()", 1)[1].split(
            "function ATK_JumpToReviewFrame()", 1)[0]
        self.assertEqual(sequential_body.count("var transport = new ATK_Transport();"), 1)
        self.assertIn('ATK_VerifyApi(transport);', sequential_body)
        self.assertIn('transport.request("get_status", {}, 5000);', sequential_body)
        self.assertEqual(sequential_body.count("ATK_VerifyApi(transport);"), 2)
        self.assertNotIn("ATK_ExportMovie", sequential_body)
        self.assertNotIn("sendMsg", SCRIPT)
        self.assertNotIn("receiveMsg", SCRIPT)
        self.assertIn("this.socket.send(encoded)", SCRIPT)
        self.assertIn("this.socket.receive(timeoutMs || 5000)", SCRIPT)

    def test_script_editor_file_exists_property_and_cleanup(self):
        class Filesystem:
            def __init__(self, exists):
                self.exists = exists
                self.remove_calls = 0
            def file(self, path):
                filesystem = self
                class File:
                    @property
                    def exists(self): return filesystem.exists
                    def remove(self): filesystem.remove_calls += 1
                return File()

        present = Filesystem(True)
        validate_exported_movie(present.file, "review.mov")
        remove_previous_movie(present.file, "previous.mov")
        self.assertEqual(present.remove_calls, 1)

        missing = Filesystem(False)
        with self.assertRaisesRegex(RuntimeError, "Harmony did not create the review movie"):
            validate_exported_movie(missing.file, "review.mov")
        remove_previous_movie(missing.file, "previous.mov")
        self.assertEqual(missing.remove_calls, 0)

        self.assertNotIn(".exists()", SCRIPT)
        self.assertIn("if (!exportedMovie.exists)", SCRIPT)
        self.assertIn("if (previous.exists) previous.remove();", SCRIPT)

    def test_review_directory_first_use_postcondition(self):
        class Filesystem:
            def __init__(self, initially_exists, creation_succeeds):
                self.exists = initially_exists
                self.creation_succeeds = creation_succeeds
                self.mkdir_calls = 0
                self.export_calls = 0
            def directory(self, path):
                filesystem = self
                class Directory:
                    @property
                    def exists(self): return filesystem.exists
                    def mkdirs(self):
                        filesystem.mkdir_calls += 1
                        if filesystem.creation_succeeds: filesystem.exists = True
                        return None
                return Directory()
            def export(self): self.export_calls += 1

        for initially_exists, creation_succeeds, expected_mkdirs in (
                (True, False, 0), (False, True, 1)):
            filesystem = Filesystem(initially_exists, creation_succeeds)
            ensure_review_directory(filesystem.directory, "temp/ATK_Player/Harmony")
            filesystem.export()
            self.assertEqual(filesystem.mkdir_calls, expected_mkdirs)
            self.assertEqual(filesystem.export_calls, 1)

        failed = Filesystem(False, False)
        with self.assertRaisesRegex(RuntimeError, "Could not create"):
            ensure_review_directory(failed.directory, "temp/ATK_Player/Harmony")
        self.assertEqual(failed.mkdir_calls, 1)
        self.assertEqual(failed.export_calls, 0)
        self.assertIn("directory.mkdirs();", SCRIPT)
        self.assertIn("directory = new Dir(directoryPath);", SCRIPT)
        self.assertNotIn("!directory.mkdirs()", SCRIPT)

    def test_forward_and_reverse_mapping(self):
        for start, harmony, expected in ((1, 1, 0), (1, 51, 50), (45, 45, 0),
                                         (45, 51, 6), (45, 75, 30)):
            self.assertEqual(harmony_to_atk(harmony, start), expected)
            self.assertEqual(atk_to_harmony(expected, start), harmony)
        self.assertEqual((0, 75 - 45, 75 - 45 + 1), (0, 30, 31))

    def test_ndjson_encoding_and_safe_parsing_are_explicit(self):
        payload = {"id": 1, "command": "get_api_info", "params": {}}
        encoded = serialize_harmony_request(1, "get_api_info")
        self.assertFalse(encoded.endswith(("\n", "\r", "\r\n")))
        self.assertEqual(json.loads(encoded), payload)
        self.assertIn('JSON.stringify({ id: id, command: command, params: params || {} });', SCRIPT)
        self.assertNotIn('JSON.stringify({ id: id, command: command, params: params || {} }) +', SCRIPT)
        self.assertIn("RemoteCmd.send() terminates raw commands itself", SCRIPT)
        self.assertIn("JSON.parse", SCRIPT)
        self.assertNotRegex(SCRIPT, r"\beval\s*\(")
        self.assertIn('if (newline < 0)', SCRIPT)

    def test_handshake_and_workflow_order(self):
        for fragment in ('info.application !== "ATK Player"',
                         'Number(info.protocolVersion) !== 1',
                         'Number(info.frameIndexBase) !== 0'):
            self.assertIn(fragment, SCRIPT)
        commands = re.findall(r'transport\.request\("([a-z_]+)"', SCRIPT)
        workflow = ["open_media", "set_loop_range", "set_loop_enabled", "seek_frame", "show_window"]
        positions = [commands.index(command) for command in workflow]
        self.assertEqual(positions, sorted(positions))
        self.assertIn('ATK_WaitForLoaded(transport, moviePath)', SCRIPT)
        self.assertIn('ATK_WaitForFrame(transport, target)', SCRIPT)

        class FakeTransport:
            def __init__(self): self.calls = []
            def request(self, command, params):
                self.calls.append((command, params))
                if command == "get_api_info":
                    return {"application": "ATK Player", "protocolVersion": 1, "frameIndexBase": 0}
                return {"currentFrame": 6, "hasMedia": True}
        fake = FakeTransport()
        model_review_workflow(fake, "C:/review.mov", 51, 45, 75)
        self.assertEqual([call[0] for call in fake.calls], [
            "get_api_info", "open_media", "get_status", "set_loop_range",
            "set_loop_enabled", "seek_frame", "get_status", "show_window"])
        self.assertEqual(fake.calls[3][1], {"start": 0, "end": 30})
        self.assertEqual(fake.calls[5][1], {"frame": 6})

    def test_response_and_handshake_failures(self):
        good = '{"id":1,"ok":true,"result":{"application":"ATK Player","protocolVersion":1,"frameIndexBase":0}}\n'
        info = parse_response(good); verify_info(info)
        for invalid in ('{"id":1,"ok":true}', "not-json\n",
                        '{"id":1,"ok":false,"error":"disabled"}\n'):
            with self.assertRaises((ValueError, json.JSONDecodeError)):
                parse_response(invalid)
        for info in ({"application": "Other", "protocolVersion": 1, "frameIndexBase": 0},
                     {"application": "ATK Player", "protocolVersion": 2, "frameIndexBase": 0},
                     {"application": "ATK Player", "protocolVersion": 1, "frameIndexBase": 1}):
            with self.assertRaises(ValueError): verify_info(info)

    def test_failure_safety_and_documented_harmony_apis(self):
        export_position = SCRIPT.index("ATK_ExportMovie(start, end)")
        open_position = SCRIPT.index('transport.request("open_media"')
        self.assertLess(export_position, open_position)
        self.assertIn('codec: "openH264"', SCRIPT)
        self.assertIn("withSound: true", SCRIPT)
        self.assertIn("scene.currentResolutionX()", SCRIPT)
        self.assertIn("scene.currentResolutionY()", SCRIPT)
        self.assertIn("frame.setCurrent(Math.round(destination))", SCRIPT)
        self.assertNotIn("Process2", SCRIPT)


if __name__ == "__main__":
    unittest.main()
