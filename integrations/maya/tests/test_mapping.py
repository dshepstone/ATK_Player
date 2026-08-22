import unittest

from atk_maya import maya_frame_to_atk_index, playblast_to_atk


class MayaMappingTests(unittest.TestCase):
    def test_professional_frame_ranges(self):
        self.assertEqual(maya_frame_to_atk_index(1042, 1001), 41)
        self.assertEqual(maya_frame_to_atk_index(1, 1), 0)
        self.assertEqual(maya_frame_to_atk_index(0, -10), 10)

    def test_playblast_waits_for_relative_seek_before_showing(self):
        class Commands:
            def playbackOptions(self, q=False, minTime=False, maxTime=False):
                return 1001 if minTime else 1100
            def currentTime(self, q=False): return 1042
            def playblast(self, **kwargs): return kwargs["filename"]
        class Player:
            def __init__(self): self.calls = []
            def open_media(self, path, discard_unsaved=False): self.calls.append(("open", path))
            def wait_until_loaded(self, path): self.calls.append(("loaded", path))
            def set_review_range(self, start, end): self.calls.append(("range", start, end))
            def set_loop_enabled(self, enabled): self.calls.append(("loop", enabled))
            def seek_frame(self, frame): self.calls.append(("seek", frame))
            def wait_until_frame(self, frame): self.calls.append(("frame", frame))
            def show_window(self): self.calls.append(("show",))
        player = Player()
        playblast_to_atk(player=player, output_path="test.avi", cmds_module=Commands())
        self.assertEqual(player.calls[-3:], [("seek", 41), ("frame", 41), ("show",)])


if __name__ == "__main__": unittest.main()
