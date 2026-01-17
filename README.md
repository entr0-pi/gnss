# UM980 PlatformIO Project

## Goals
- Provide a clean PlatformIO-based firmware workspace for the UM980 device.
- Separate hardware-facing firmware from web assets and supporting scripts.
- Make it straightforward to build, upload, and extend the project structure.

## Features
- **PlatformIO configuration** with a single entry point in `platformio.ini`.
- **Firmware source layout** under `src/` and `include/` following PlatformIO conventions.
- **Shared libraries** kept in `lib/` for reusable components.
- **Web assets** stored in `web/` for any UI or hosted files.
- **Utility scripts** in `scripts/` for automation or tooling.

## Folder Structure
```
.
├── include/        # Header files and shared declarations
├── lib/            # Reusable libraries
├── scripts/        # Build or development helper scripts
├── src/            # Firmware source files (main entry points)
├── web/            # Web UI or static assets
├── platformio.ini  # PlatformIO project configuration
└── README.md       # Project documentation
```

## Additional Information
- Build and upload with PlatformIO using the standard `pio run` and `pio run -t upload` commands.
- When adding new code, prefer keeping device logic in `src/` and generic helpers in `lib/`.
- If you add a frontend, keep assets in `web/` and document any build steps here.
