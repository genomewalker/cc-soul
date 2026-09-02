import os
import unittest

from client import get_api_key


class TestClientCredentialEnvVar(unittest.TestCase):
    def setUp(self):
        self._saved = {
            k: os.environ.pop(k, None)
            for k in ("FORGE_API_TOKEN", "API_KEY", "FORGE_API_KEY")
        }

    def tearDown(self):
        for k, v in self._saved.items():
            if v is not None:
                os.environ[k] = v
            else:
                os.environ.pop(k, None)

    def test_reads_the_project_specific_env_var(self):
        os.environ["FORGE_API_TOKEN"] = "abc123"
        self.assertEqual(get_api_key(), "abc123")

    def test_does_not_fall_back_to_the_generic_name(self):
        os.environ["API_KEY"] = "wrong-source"
        self.assertNotEqual(get_api_key(), "wrong-source")


if __name__ == "__main__":
    unittest.main()
