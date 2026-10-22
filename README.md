# AdrenoFirmware

Adreno GMU firmware kit. Builds the HFI transport, startup sequence and power votes, and embeds the GMU and SQE firmware.

Current state: GMU cold start and HFI work on sheng hardware (Adreno 740). The Adreno 650 startup configuration (`AdrenoA650.c`, generated `AdrenoA650Source.h`), the zap shader load plan (`AdrenoZap.c`) and the embedding tool with the a650 pins (`AdrenoFirmwareEmbed.py`, `a650-firmware.json`) are untested on hardware.

Built into the kernel with the device GPU stack. `test-host.py` builds and runs the kit tests with a plain clang.
