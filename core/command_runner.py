"""
core/command_runner.py
Safe subprocess execution helpers.

- Commands are always passed as argument lists (never shell=True) to avoid
  command-injection risk.
- Long-running commands (e.g. logcat) can be started as a managed background
  process that can be polled / terminated from any thread.
- Short commands can be run synchronously (with a timeout) or asynchronously
  via a callback delivered on a background thread; callers are responsible
  for marshalling the callback back onto the Tk main thread (e.g. via
  `root.after`).
"""

from __future__ import annotations

import subprocess
import threading
from dataclasses import dataclass
from typing import Callable, List, Optional

from utils.logger import get_logger

logger = get_logger(__name__)

# Prevents a console window from flashing on Windows when we spawn adb.
_CREATE_NO_WINDOW = 0x08000000


@dataclass
class CommandResult:
    command: List[str]
    returncode: int
    stdout: str
    stderr: str
    timed_out: bool = False
    error: Optional[str] = None

    @property
    def success(self) -> bool:
        return self.error is None and not self.timed_out and self.returncode == 0


class CommandRunner:
    """Runs ADB (or any) commands as argument lists via subprocess."""

    def __init__(self) -> None:
        self._processes_lock = threading.Lock()
        self._managed_processes: dict[str, subprocess.Popen] = {}

    # ------------------------------------------------------------------ #
    # Synchronous execution
    # ------------------------------------------------------------------ #
    def run(self, args: List[str], timeout: float = 15.0) -> CommandResult:
        """Run a command synchronously and capture its output."""
        logger.debug("Running command: %s", " ".join(args))
        try:
            completed = subprocess.run(
                args,
                capture_output=True,
                text=True,
                timeout=timeout,
                creationflags=_CREATE_NO_WINDOW if _is_windows() else 0,
            )
            return CommandResult(
                command=args,
                returncode=completed.returncode,
                stdout=completed.stdout or "",
                stderr=completed.stderr or "",
            )
        except subprocess.TimeoutExpired:
            logger.warning("Command timed out: %s", " ".join(args))
            return CommandResult(args, -1, "", "", timed_out=True, error="Command timed out")
        except FileNotFoundError:
            logger.error("Executable not found for command: %s", " ".join(args))
            return CommandResult(args, -1, "", "", error="Executable not found")
        except OSError as exc:
            logger.error("OS error running command %s: %s", args, exc)
            return CommandResult(args, -1, "", "", error=str(exc))

    # ------------------------------------------------------------------ #
    # Asynchronous (fire-and-forget with callback) execution
    # ------------------------------------------------------------------ #
    def run_async(
        self,
        args: List[str],
        callback: Callable[[CommandResult], None],
        timeout: float = 30.0,
    ) -> threading.Thread:
        """
        Run a command on a background thread and invoke `callback` with the
        CommandResult once finished. `callback` runs on the worker thread —
        the caller must marshal it back to the UI thread if touching Tk widgets.
        """

        def _worker() -> None:
            result = self.run(args, timeout=timeout)
            callback(result)

        thread = threading.Thread(target=_worker, daemon=True)
        thread.start()
        return thread

    # ------------------------------------------------------------------ #
    # Long-running managed processes (e.g. `adb logcat`)
    # ------------------------------------------------------------------ #
    def start_managed_process(self, key: str, args: List[str]) -> subprocess.Popen:
        """
        Start a long-running process tracked under `key` so it can later be
        polled for output or terminated with `stop_managed_process`.
        """
        self.stop_managed_process(key)  # ensure no duplicate is left running
        logger.info("Starting managed process [%s]: %s", key, " ".join(args))
        proc = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            creationflags=_CREATE_NO_WINDOW if _is_windows() else 0,
        )
        with self._processes_lock:
            self._managed_processes[key] = proc
        return proc

    def stop_managed_process(self, key: str) -> None:
        with self._processes_lock:
            proc = self._managed_processes.pop(key, None)
        if proc and proc.poll() is None:
            logger.info("Stopping managed process [%s]", key)
            try:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
            except OSError as exc:
                logger.warning("Error stopping process [%s]: %s", key, exc)

    def is_process_running(self, key: str) -> bool:
        with self._processes_lock:
            proc = self._managed_processes.get(key)
        return proc is not None and proc.poll() is None

    def stop_all(self) -> None:
        """Terminate every managed process. Call this on application exit."""
        with self._processes_lock:
            keys = list(self._managed_processes.keys())
        for key in keys:
            self.stop_managed_process(key)


def _is_windows() -> bool:
    import sys

    return sys.platform.startswith("win")
