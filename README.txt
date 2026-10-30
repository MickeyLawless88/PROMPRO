PROMPRO v1.6 - EPROM Data Reader and Hex Editor      (c) 2025 Mickey W. Lawless

One source file (source/prompro.c) builds for all three platforms.

WINDOWS   windows\PROMPRO.EXE (64-bit) or PROMPRO32.EXE (32-bit). Icon embedded.
          Run from cmd/PowerShell/Windows Terminal. Resize the window freely;
          F11 maximizes. Port: Options > COM Port, or  PROMPRO /COM3

MAC       macos/prompro-macos-arm64 (Apple Silicon) or -x86_64 (Intel).
          chmod +x prompro-macos-arm64
          xattr -d com.apple.quarantine prompro-macos-arm64     (if Gatekeeper complains)
          ./prompro-macos-arm64 [-p /dev/cu.usbserial-XXXX] [file.bin]
          Or build natively:  cd source && make
          Alt+letter needs Terminal > Settings > Profiles > Keyboard >
          "Use Option as Meta key". Without it, Option+S/D/P/T/O/H still work
          and F2..F8 always open the menus (Setup Data Program Tasks Utilities
          Options Exit). Settings are kept in ~/.prompro/

LINUX     linux/prompro-linux-x86_64 or -arm64. Add yourself to the dialout
          group for /dev/ttyUSB0 access. Same keys and options as the Mac.

MS-DOS    dos\PROMPRO.C + dos\BUILD.BAT  (Turbo C++ 1.0, medium model).
          Copy both to a folder on the DOS side and run  BUILD  (or BUILD D:\TC
          if Turbo C is not in C:\TC). COM1-COM4 on IRQ4/IRQ3, interrupt driven,
          16550 FIFO used when present. F11 switches 25 <-> 50 line text mode.

Serial format expected from the programmer: ASCII, space-separated hex bytes.
