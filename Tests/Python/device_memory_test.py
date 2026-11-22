"""Pins the memory placement settings that device.py writes for devices with SPIRAM.

Run with: python3 -m unittest discover -s Tests/Python -p '*_test.py'

These tests fail on purpose when a value changes. Read this before updating a value, and update the test with it.

SPIRAM_MALLOC_ALWAYSINTERNAL (8192)
    malloc(), new and the std containers put an allocation of up to this many bytes in internal RAM, and a larger
    one in SPIRAM (heap_caps.c compares with <=). Internal RAM is scarce, so a lower value frees more of it.
    A lower value also moves more allocations of code that doesn't pick its memory into SPIRAM, which is risky:
    - DMA: drivers that pass a malloc() buffer to SPI, I2S or SDMMC without MALLOC_CAP_DMA. SPIRAM needs cache line
      alignment for DMA on ESP32-S3 and newer, and classic ESP32 can't do DMA from SPIRAM at all.
    - Flash writes (OTA, NVS, erase) disable the cache, which makes SPIRAM unreadable. Data that an IRAM interrupt
      handler uses during a flash write must be in internal RAM.
    - SPIRAM is several times slower than internal RAM on a cache miss.
    Driver state and interrupt data are almost always smaller than 8 KB, so 8192 leaves them in internal RAM.
    Classic ESP32 keeps the IDF default of 16384: it has the SPIRAM cache errata, no DMA from SPIRAM and the least
    internal RAM to win back.

FATFS_VFS_FSTAT_BLKSIZE (16384, classic ESP32: 4096)
    The stdio buffer that newlib allocates for every open FAT file on its first read or write, with exactly this
    size. The IDF default of 128 bytes makes every small fread(), fwrite() or fprintf() a read-modify-write of a
    512 byte sector of the SD card, which is slow.
    It must be larger than SPIRAM_MALLOC_ALWAYSINTERNAL, or each open file takes internal RAM. Equal is not enough.
    On classic ESP32 the buffer is in internal RAM, so it's smaller there.
"""

import io
import os
import sys
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))

import device  # noqa: E402


def generate_spiram_config(target):
    """The sdkconfig lines of write_spiram_variables() for a device with SPIRAM, as a dict"""
    output = io.StringIO()
    device.write_spiram_variables(output, {
        "hardware.target": target,
        "hardware.spiRam": "true",
        "hardware.spiRamMode": "OCT",
        "hardware.spiRamSpeed": "80M",
    })
    config = {}
    for line in output.getvalue().splitlines():
        if line.startswith("CONFIG_"):
            key, _, value = line.partition("=")
            config[key] = value
    return config


class DeviceMemoryTest(unittest.TestCase):
    def test_values(self):
        """A change needs a review against the reasons in this file's docstring"""
        self.assertEqual(device.SPIRAM_MALLOC_ALWAYSINTERNAL, 8192)
        self.assertEqual(device.FATFS_FILE_BUFFER_SIZE, 16384)
        self.assertEqual(device.FATFS_FILE_BUFFER_SIZE_ESP32, 4096)

    def test_file_buffer_is_in_spiram(self):
        """heap_caps.c puts an allocation of exactly the threshold in internal RAM"""
        self.assertGreater(device.FATFS_FILE_BUFFER_SIZE, device.SPIRAM_MALLOC_ALWAYSINTERNAL)

    def test_newer_targets(self):
        """Every target except classic ESP32 gets the lower threshold, so new chip families are included"""
        for target in ["ESP32S3", "ESP32P4"]:
            with self.subTest(target=target):
                config = generate_spiram_config(target)
                self.assertEqual(config.get("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL"), "8192")
                self.assertEqual(config.get("CONFIG_FATFS_VFS_FSTAT_BLKSIZE"), "16384")

    def test_classic_esp32(self):
        """Classic ESP32 keeps the IDF default threshold, so its file buffer is in internal RAM"""
        config = generate_spiram_config("ESP32")
        self.assertNotIn("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL", config)
        self.assertEqual(config.get("CONFIG_FATFS_VFS_FSTAT_BLKSIZE"), "4096")


if __name__ == "__main__":
    unittest.main()
