import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import avatar_boot_assets as boot  # noqa: E402

AVATARS = ROOT / "main" / "boards" / "elena-stackchan" / "avatars"


class EncodingTests(unittest.TestCase):
    def test_opaque_red_pixel(self):
        self.assertEqual(boot.encode_rgb565a8(bytes((255, 0, 0, 255)), 1, 1), bytes((0x00, 0xF8, 0xFF)))

    def test_transparent_pixel_keeps_alpha_zero(self):
        self.assertEqual(boot.encode_rgb565a8(bytes((0, 0, 0, 0)), 1, 1), bytes((0, 0, 0)))

    def test_all_colors_come_before_all_alpha(self):
        rgba = bytes((255, 0, 0, 10, 0, 0, 255, 20))
        self.assertEqual(boot.encode_rgb565a8(rgba, 2, 1), bytes((0x00, 0xF8, 0x1F, 0x00, 10, 20)))

    def test_wrong_size_is_refused(self):
        with self.assertRaises(ValueError):
            boot.encode_rgb565a8(bytes(3), 1, 1)


class CSourceTests(unittest.TestCase):
    def test_c_output_declares_the_image(self):
        text = boot.render_image_c("avatar_boot_image", bytes(12), 4, 1)
        for expected in (
            "const lv_image_dsc_t avatar_boot_image",
            "LV_COLOR_FORMAT_RGB565A8",
            "LV_IMAGE_HEADER_MAGIC",
            ".w = 4",
            ".h = 1",
            ".stride = 8",
            ".data_size = 12",
        ):
            self.assertIn(expected, text)

    def test_c_output_holds_every_byte(self):
        text = boot.render_image_c("avatar_boot_name", bytes(range(12)), 4, 1)
        self.assertEqual(len(re.findall(r"0x[0-9a-f]{2}", text)), 12)


class RealAssetsTests(unittest.TestCase):
    def check(self, path, symbol, max_width, max_height):
        text = path.read_text(encoding="utf-8")
        self.assertIn(f"const lv_image_dsc_t {symbol}", text)
        width = int(re.search(r"\.w = (\d+)", text)[1])
        height = int(re.search(r"\.h = (\d+)", text)[1])
        size = int(re.search(r"\.data_size = (\d+)", text)[1])
        self.assertEqual(size, width * height * 3)
        self.assertEqual(len(re.findall(r"0x[0-9a-f]{2}", text)), size)
        self.assertLessEqual(width, max_width)
        self.assertLessEqual(height, max_height)

    def test_each_avatar_has_its_boot_assets(self):
        for name in ("elena", "emoji"):
            with self.subTest(name):
                self.assertTrue((AVATARS / name / "boot.png").exists())
                self.check(AVATARS / name / "boot_image.c", "avatar_boot_image", 132, 123)
                self.check(AVATARS / name / "boot_name.c", "avatar_boot_name", 300, 44)

    def test_font_keeps_its_license(self):
        self.assertTrue((AVATARS / "fonts" / "CherryBombOne-Regular.ttf").exists())
        self.assertIn("SIL Open Font License", (AVATARS / "fonts" / "OFL.txt").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
