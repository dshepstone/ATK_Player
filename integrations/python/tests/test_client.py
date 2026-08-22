import json
import socket
import threading
import unittest

from atk_player import AtkPlayer, AtkCommandError, AtkProtocolError, AtkTimeoutError


class StubServer:
    def __init__(self, responder):
        self.responder = responder
        self.socket = socket.socket()
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen(1)
        self.port = self.socket.getsockname()[1]
        self.thread = threading.Thread(target=self.run, daemon=True)

    def __enter__(self): self.thread.start(); return self
    def __exit__(self, *_): self.socket.close(); self.thread.join(1)

    def run(self):
        connection, _ = self.socket.accept()
        with connection:
            stream = connection.makefile("rb")
            for line in stream:
                request = json.loads(line.decode("utf-8"))
                response = self.responder(request)
                connection.sendall(json.dumps(response).encode("utf-8") + b"\n")


class ClientTests(unittest.TestCase):
    def test_sequential_unicode_and_command_error(self):
        def reply(request):
            if request["command"] == "add_bookmark":
                self.assertEqual(request["params"]["note"], "Épaulement")
                return {"id": request["id"], "ok": False, "error": "refused"}
            return {"id": request["id"], "ok": True, "result": {"state": "ready"}}
        with StubServer(reply) as server, AtkPlayer(port=server.port) as player:
            self.assertEqual(player.status()["state"], "ready")
            with self.assertRaises(AtkCommandError):
                player.add_bookmark(note="Épaulement")

    def test_mismatched_id_is_protocol_error(self):
        with StubServer(lambda request: {"id": request["id"] + 1, "ok": True, "result": {}}) as server:
            with AtkPlayer(port=server.port) as player:
                with self.assertRaises(AtkProtocolError): player.status()

    def test_wait_until_frame_uses_authoritative_zero_based_status(self):
        frames = iter((9, 9, 10))
        def reply(request):
            self.assertEqual(request["command"], "get_status")
            frame = next(frames)
            return {"id": request["id"], "ok": True,
                    "result": {"currentFrame": frame, "displayFrame": frame + 1}}
        with StubServer(reply) as server, AtkPlayer(port=server.port) as player:
            status = player.wait_until_frame(10, timeout=1.0, poll_interval=0.001)
            self.assertEqual(status["currentFrame"], 10)
            self.assertEqual(status["displayFrame"], 11)

    def test_wait_until_frame_times_out(self):
        def reply(request):
            return {"id": request["id"], "ok": True,
                    "result": {"currentFrame": 0, "displayFrame": 1}}
        with StubServer(reply) as server, AtkPlayer(port=server.port) as player:
            with self.assertRaises(AtkTimeoutError):
                player.wait_until_frame(1, timeout=0.02, poll_interval=0.001)


if __name__ == "__main__": unittest.main()
