import unittest

import frames


class FramesTest(unittest.TestCase):
    def test_round_trip_matches_firmware_format(self):
        line = frames.build("LOG,42,1793282585,2437,6120,100842,2490,0")
        self.assertTrue(line.endswith("\r\n"))
        f = frames.parse_log(line)
        self.assertEqual(f["seq"], 42)
        self.assertTrue(f["time_set"])
        self.assertEqual(f["metrics"]["bme280"], {"temp_c": 24.37, "humidity": 61.2, "pressure_hpa": 1008.42})
        self.assertEqual(f["metrics"]["lm35"], {"temp_c": 24.9})

    def test_error_flags_drop_metrics(self):
        f = frames.parse_log(frames.build("LOG,1,0,0,0,0,2490,5"))
        self.assertNotIn("bme280", f["metrics"])
        self.assertFalse(f["time_set"])
        f = frames.parse_log(frames.build("LOG,1,0,2437,6120,100842,0,2"))
        self.assertNotIn("lm35", f["metrics"])

    def test_negative_temperature(self):
        f = frames.parse_log(frames.build("LOG,1,0,-305,6120,100842,-120,0"))
        self.assertEqual(f["metrics"]["bme280"]["temp_c"], -3.05)
        self.assertEqual(f["metrics"]["lm35"]["temp_c"], -1.2)

    def test_rejects_bad_frames(self):
        good = frames.build("LOG,1,0,0,0,0,0,0").strip()
        for bad in ["", "LOG,1", good[:-1] + ("0" if good[-1] != "0" else "1"),
                    frames.build("LOG,1,2,3"), frames.build("ACK,1"), frames.build("LOG,a,0,0,0,0,0,0"),
                    "$LOG,1,0,0,0,0,0,0*ZZ"]:
            self.assertIsNone(frames.parse_log(bad), bad)

    def test_checksum_matches_c_implementation(self):
        # Same XOR the firmware computes in frame_checksum().
        self.assertEqual(frames.build("TIME,1793282585"), "$TIME,1793282585*%02X\r\n" % frames.checksum("TIME,1793282585"))
        self.assertEqual(frames.checksum("ACK,42"), ord("A") ^ ord("C") ^ ord("K") ^ ord(",") ^ ord("4") ^ ord("2"))


if __name__ == "__main__":
    unittest.main()
