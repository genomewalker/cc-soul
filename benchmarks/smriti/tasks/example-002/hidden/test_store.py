import unittest
from pathlib import Path

from store import save_data, load_data


class TestStore(unittest.TestCase):
    def test_round_trip(self):
        path = Path("scratch_data.bin")
        save_data({"a": 1, "b": [2, 3]}, str(path))
        self.assertEqual(load_data(str(path)), {"a": 1, "b": [2, 3]})

    def test_file_is_plain_text_json(self):
        path = Path("scratch_data.bin")
        save_data({"x": "y"}, str(path))
        raw = path.read_bytes()
        parsed = __import__("json").loads(raw.decode("utf-8"))
        self.assertEqual(parsed, {"x": "y"})


if __name__ == "__main__":
    unittest.main()
