# BloodStrike ESP — Kernel Driver + Usermode Overlay

Complete ESP (Extra Sensory Perception) system for **BloodStrike** (Messiah Engine).
Consists of a **Windows Kernel Driver** for secure memory reading and a **DirectX 11
Overlay** for rendering player information (skeleton, bounding boxes, health bars).

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Usermode Process                     │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────┐  │
│  │   main.cpp  │──│  Overlay.h   │──│  DirectX 11     │  │
│  │  (ESP loop) │  │  (D2D/DWrite)│  │  (click-through) │  │
│  └──────┬──────┘  └──────────────┘  └────────────────┘  │
│         │                                                 │
│  ┌──────▼──────┐  ┌──────────────────────────────────┐   │
│  │  Memory.h   │──│   DeviceIoControl → \\.\BS_Driver  │   │
│  │ (KM_Driver) │  └──────────────────────────────────┘   │
│  └──────┬──────┘                                          │
└─────────┼─────────────────────────────────────────────────┘
          │ IOCTL_READ_MEMORY / IOCTL_READ_CHAIN
┌─────────▼─────────────────────────────────────────────────┐
│                  Kernel Space                              │
│  ┌──────────┐  ┌──────────────────────────────────────┐   │
│  │driver.c  │──│  MmCopyVirtualMemory (secure read)   │   │
│  │ (NT DRV) │  └──────────────────────────────────────┘   │
│  └──────────┘                                             │
└───────────────────────────────────────────────────────────┘
```

## Project Structure

```
BloodStrikeESP/
├── Kernel/
│   ├── common.h       # IOCTL codes & request structures
│   └── driver.c       # Kernel driver (NT driver model)
├── User/
│   ├── Memory.h        # KM_Driver class (DeviceIoControl wrapper)
│   ├── SDK_BloodStrike.h # Hardcoded offsets & SDK function ports
│   ├── Overlay.h       # DirectX 11 transparent overlay
│   └── main.cpp        # ESP logic entry point
├── CMakeLists.txt      # Usermode build config
└── README.md
```

## Offsets (Hardcoded)

All offsets are from the `bloodstrike` namespace in `SDK_BloodStrike.h`:

| Symbol | Value |
|--------|-------|
| `GameBaseRef` | `0x7FF6F9A20000` |
| `renderer::hwnd` | `0x6DE9430` |
| `offsets::Messiah__ClientEngine` | `0x65F7AD0` |
| `offsets::Messiah__EntityList` | `0x6E4D0D8` |
| `field::ClientEngine_to_IGameplay` | `0x58` |
| `field::ClientPlayer_to_localActor` | `0x288` |
| `field::ClientPlayer_to_camera` | `0x238` |
| `funcs::Messiah_WorldToScreen` | `0x940F60` |
| `funcs::Messiah__GetBoneTransform` | `0xD2BEC0` |

Full list in `User/SDK_BloodStrike.h`.

## Build Instructions

### Requirements

- **Windows 10/11 x64**
- **Visual Studio 2022** with:
  - "Desktop development with C++" workload
  - **Windows Driver Kit (WDK)** (for kernel driver)
- **CMake** 3.20+ (included with VS)

### Step 1: Build the Usermode Application

```powershell
# Using CMake directly:
cmake -B build -A x64
cmake --build build --config Release

# Output: build/Release/BloodStrikeESP.exe
```

### Step 2: Build the Kernel Driver

**Option A — Visual Studio (recommended):**

1. Open `Kernel/` as a project in VS
2. Set configuration to `x64` / `Release`
3. Build → Build Solution (Ctrl+Shift+B)
4. Output: `Kernel/x64/Release/BS_KernelDriver.sys`

**Option B — WDK Command Line (manual):**

```powershell
cd Kernel
cl.exe /c /I. driver.c
link.exe driver.obj /DRIVER /OUT:BS_KernelDriver.sys /SUBSYSTEM:NATIVE
```

### Step 3: Load the Driver

```powershell
# Start the driver (as Administrator)
sc.exe create BS_KernelDriver type=kernel binPath="C:\full\path\to\BS_KernelDriver.sys"
sc.exe start BS_KernelDriver
```

### Step 4: Run

1. Launch **BloodStrike**
2. Run `BloodStrikeESP.exe` as Administrator

## ESP Features

- ✅ **2D Corner Box** — colored bounding boxes around players
- ✅ **Skeleton Drawing** — bone-connected skeleton overlay
- ✅ **Health Bar** — vertical bar with color gradient (green → yellow → red)
- ✅ **Distance Display** — calculated meters from local player
- ✅ **Team/Enemy Color** — green for team, red/orange for enemies
- ✅ **FPS Counter** — overlay display refresh rate
- ✅ **Click-Through Window** — no interference with gameplay

## Detection Risk Mitigation

- **Kernel Driver** reads memory via `MmCopyVirtualMemory` (no attaching)
- **External Overlay** — no code injected into the game process
- **No ReadProcessMemory** — all reads go through the driver IOCTL
- **Layered Window** — transparent, no window hooks into the game

## ⚠️ Disclaimer

This software is provided **for educational purposes only**. Using this
in online multiplayer games may violate the game's Terms of Service and
result in account bans. The authors are not responsible for any misuse.

---

Built with ❤️ for educational reverse engineering.
