# FAKE POD NANO PRO S31 V1

Work in progress on `feat/fakepod-s31`; this profile is not yet a completed peripheral qualification.

- Native ESP32-S31, dual-core RISC-V with FPU; separate ESP-IDF release/v6.1 installation.
- Actual device: 16 MiB flash, 16 MiB PSRAM at 200 MHz; PSRAM memory test passed.
- CO5300 QSPI 460×460 RGB565, horizontal RAM offset 10; six color bars confirmed on hardware.
- CST820 touch detected. CS43131 full part identity and crystal readiness verified; audio output and PDM capture still need functional validation.
- SDMMC 4-bit mount verified with automatic formatting disabled.
- Standalone native AOT integer and floating-point execution verified from PSRAM. Production runtime requires AOT and retains the bounded interruptible profile.
- Aster first-boot UI, native Wi-Fi and USB Serial/JTAG console start on hardware. First-boot resource installation is being validated.
- AXP2101 power management, QMI8658 IMU and PCF85063 RTC share I²C0; their addresses respond, full Aster service integration remains pending.

Pin definitions follow the supplied V1 hardware documentation and schematic. Reference archive, extracted files and diagnostic outputs stay outside version control.

Build using ESP-IDF 6.1 with `idf.py --preview`, target `esp32s31`, and an out-of-tree build directory. Production entry is the default; `-DASTER_S31_DIAGNOSTIC=ON` selects the standalone probe (requires the locally generated AOT test fixture).

The OBS channel includes this board's bootstrap profile (`fakepod-s31`, `riscv`, 460×460). The initial catalog contains TouchTest; other application/board combinations require validation before publication.
