import json
import re
import unittest
from pathlib import Path


SCRIPT = (Path(__file__).parents[1] / "scripts" / "ATK_Review.js").read_text(encoding="utf-8")


def harmony_to_atk(frame, start):
    return int(frame) - int(start)


def atk_to_harmony(frame, start):
    return int(start) + int(frame)


def parse_response(raw, request_id=1):
    if "\n" not in raw:
        raise ValueError("incomplete")
    response = json.loads(raw.split("\n", 1)[0])
    if response.get("id") != request_id or response.get("ok") is not True:
        raise ValueError(response.get("error", "invalid response"))
    return response.get("result", {})


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


class HarmonyScriptTests(unittest.TestCase):
    def test_forward_and_reverse_mapping(self):
        for start, harmony, expected in ((1, 1, 0), (1, 51, 50), (45, 45, 0),
                                         (45, 51, 6), (45, 75, 30)):
            self.assertEqual(harmony_to_atk(harmony, start), expected)
            self.assertEqual(atk_to_harmony(expected, start), harmony)
        self.assertEqual((0, 75 - 45, 75 - 45 + 1), (0, 30, 31))

    def test_ndjson_encoding_and_safe_parsing_are_explicit(self):
        payload = {"id": 1, "command": "get_api_info", "params": {}}
        encoded = json.dumps(payload, separators=(",", ":")) + "\n"
        self.assertTrue(encoded.endswith("\n"))
        self.assertEqual(json.loads(encoded), payload)
        self.assertIn('JSON.stringify({ id: id, command: command, params: params || {} }) + "\\n"', SCRIPT)
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
