"""Host tests for cyd_push.py. Run with: python3 tools/test_cyd_push.py"""
import hashlib
import hmac
import os
import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cyd_push  # noqa: E402

SHA = hashlib.sha256(b"").hexdigest()


def sample_image():
    image = bytearray(256)
    image[0] = 0xE9
    image[32:36] = bytes([0x32, 0x54, 0xCD, 0xAB])
    image[48:48 + 18] = b"1793027864-1dec0ea"
    image[80:80 + 14] = b"home_dashboard"
    return bytes(image)


class NormalizeKey(unittest.TestCase):
    def test_lowercase_with_dashes(self):
        self.assertEqual(cyd_push.normalize_key("abcd-1234-efgh-5678-jkmn"), "ABCD1234EFGH5678JKMN")

    def test_spaces_and_o_read_as_zero(self):
        self.assertEqual(cyd_push.normalize_key(" ABCD 1234 EFGH 5678 JKMO\n"), "ABCD1234EFGH5678JKM0")

    def test_i_l_o_read_as_digits(self):
        self.assertEqual(cyd_push.normalize_key("ilo0-1234-efgh-5678-jkmn"), "11001234EFGH5678JKMN")

    def test_too_short_raises(self):
        with self.assertRaises(ValueError):
            cyd_push.normalize_key("ABCD-1234")

    def test_bad_letter_raises(self):
        with self.assertRaises(ValueError):
            cyd_push.normalize_key("ABCD-1234-EFGH-5678-JKMU")


class NewKey(unittest.TestCase):
    def test_shape(self):
        key = cyd_push.new_key()
        self.assertRegex(key, r"^[0-9A-HJKMNP-TV-Z]{4}(-[0-9A-HJKMNP-TV-Z]{4}){4}$")
        self.assertEqual(len(cyd_push.normalize_key(key)), 20)

    def test_calls_differ(self):
        self.assertNotEqual(cyd_push.new_key(), cyd_push.new_key())


class AuthMessage(unittest.TestCase):
    def test_message_and_hmac(self):
        message = cyd_push.auth_message("00112233445566778899aabbccddeeff", 1187968, SHA,
                                        "home_dashboard", 1761488715)
        expected = ("cyd-update-1\n00112233445566778899aabbccddeeff\n1187968\n" + SHA +
                    "\nhome_dashboard\n1761488715\n").encode()
        self.assertEqual(message, expected)
        digest = hmac.new(b"ABCD1234EFGH5678JKMN", message, hashlib.sha256).hexdigest()
        self.assertEqual(digest, "cb329fbcc91a59decaaa1d633be21bfea84b329c008c30f8edd4f1ae9d660e80")


class ReadProject(unittest.TestCase):
    def test_reads_name(self):
        self.assertEqual(cyd_push.read_project(sample_image()), "home_dashboard")

    def test_bootloader_shaped_raises(self):
        image = bytearray(256)
        image[0] = 0xE9
        image[1] = 0x03
        with self.assertRaises(ValueError):
            cyd_push.read_project(bytes(image))

    def test_short_raises(self):
        with self.assertRaises(ValueError):
            cyd_push.read_project(sample_image()[:100])

    def test_unterminated_name_raises(self):
        image = bytearray(sample_image())
        image[80:112] = b"x" * 32
        with self.assertRaises(ValueError):
            cyd_push.read_project(bytes(image))

    def test_empty_name_raises(self):
        image = bytearray(sample_image())
        image[80] = 0
        with self.assertRaises(ValueError):
            cyd_push.read_project(bytes(image))


class BoardHost(unittest.TestCase):
    def test_environment_wins(self):
        self.assertEqual(cyd_push.board_host({"CYD_HOST": "cyd-e5d0.local"}, Path("/no/such/file")),
                         "cyd-e5d0.local")

    def test_host_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "host"
            path.write_text("192.168.1.126\n")
            self.assertEqual(cyd_push.board_host({}, path), "192.168.1.126")

    def test_neither_names_cyd_host(self):
        with self.assertRaises(RuntimeError) as caught:
            cyd_push.board_host({}, Path("/no/such/file"))
        self.assertIn("CYD_HOST", str(caught.exception))


class BusySeconds(unittest.TestCase):
    def test_busy(self):
        self.assertEqual(cyd_push.busy_seconds("busy 42"), 42)

    def test_other(self):
        self.assertIsNone(cyd_push.busy_seconds("installed the launcher; it runs at the next power-on"))


if __name__ == "__main__":
    unittest.main()
