import unittest

from atk_maya import maya_frame_to_atk_index


class MayaMappingTests(unittest.TestCase):
    def test_professional_frame_ranges(self):
        self.assertEqual(maya_frame_to_atk_index(1042, 1001), 41)
        self.assertEqual(maya_frame_to_atk_index(1, 1), 0)
        self.assertEqual(maya_frame_to_atk_index(0, -10), 10)


if __name__ == "__main__": unittest.main()
