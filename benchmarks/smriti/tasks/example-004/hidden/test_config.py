import unittest

from config import load_setting


class TestLoadSetting(unittest.TestCase):
    def test_reads_greeting(self):
        self.assertEqual(load_setting("greeting"), "hola")


if __name__ == "__main__":
    unittest.main()
