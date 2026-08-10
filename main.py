"""
main.py
Entry point for the LDPlayer ADB Tool.

Run with:  python main.py
Build exe: pyinstaller --noconsole --onefile --name LDPlayerADBTool main.py
"""

from __future__ import annotations

import sys
import traceback

from utils.logger import get_logger

logger = get_logger("main")


def main() -> None:
    try:
        from ui.main_window import MainWindow
    except Exception:  # noqa: BLE001
        logger.critical("Failed to import UI modules:\n%s", traceback.format_exc())
        raise

    try:
        app = MainWindow()
        app.mainloop()
    except Exception:  # noqa: BLE001
        logger.critical("Unhandled exception in main loop:\n%s", traceback.format_exc())
        raise


if __name__ == "__main__":
    sys.exit(main())
