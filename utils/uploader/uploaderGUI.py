import csv
import gzip
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
import webbrowser
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

try:
    from serial.tools import list_ports
except Exception:
    list_ports = None

# Allow importing from sibling utils directories.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "nvs-tester"))
from check_nvs_keys import compare as nvs_compare  # noqa: E402
from check_nvs_keys import parse_header as nvs_parse_header  # noqa: E402

ESPTOOL_BAUD = "921600"


def get_base_dirs():
    """Return (bundle_dir, app_dir)."""
    if getattr(sys, "frozen", False):
        bundle_dir = sys._MEIPASS
        app_dir = os.path.dirname(sys.executable)
    else:
        bundle_dir = os.path.dirname(os.path.abspath(__file__))
        app_dir = bundle_dir
    return bundle_dir, app_dir


VALID_CHIPS = ("esp32", "esp32s2", "esp32s3", "esp32c2", "esp32c3", "esp32c6", "esp32h2", "esp32p4", "esp8266")


def sanitize_chip(value: str) -> str:
    """Normalise a chip string: lowercase, strip hyphens/spaces."""
    return value.lower().replace("-", "").replace(" ", "").strip()


def validate_ipv4(value: str) -> tuple[bool, str]:
    """Validate IPv4 address format."""
    pattern = r"^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$"
    match = re.match(pattern, value)
    if not match:
        return False, "Invalid IPv4 format (expected: x.x.x.x)"
    octets = [int(g) for g in match.groups()]
    if any(o > 255 for o in octets):
        return False, "IPv4 octets must be 0-255"
    return True, ""


def validate_baud_rate(value: str) -> tuple[bool, str]:
    """Validate baud rate is a standard rate."""
    standard_rates = {50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 500000, 576000, 921600, 1000000, 2000000}
    try:
        rate = int(value)
        if rate in standard_rates:
            return True, ""
        return False, f"Non-standard baud rate. Use one of: {', '.join(str(r) for r in sorted(standard_rates))}"
    except ValueError:
        return False, "Baud rate must be a number"


def validate_pin_number(value: str) -> tuple[bool, str]:
    """Validate ESP32 pin number (0-48)."""
    try:
        pin = int(value)
        if 0 <= pin <= 48:
            return True, ""
        return False, "Pin number must be 0-48"
    except ValueError:
        return False, "Pin number must be a number"


def validate_positive_int(value: str) -> tuple[bool, str]:
    """Validate positive integer."""
    try:
        val = int(value)
        if val > 0:
            return True, ""
        return False, "Value must be a positive integer"
    except ValueError:
        return False, "Value must be a number"


def validate_boolean(value: str) -> tuple[bool, str]:
    """Validate boolean (0 or 1)."""
    if value in ("0", "1"):
        return True, ""
    return False, "Boolean value must be 0 or 1"


def validate_port(value: str) -> tuple[bool, str]:
    """Validate port number (1-65535)."""
    try:
        port = int(value)
        if 1 <= port <= 65535:
            return True, ""
        return False, "Port must be 1-65535"
    except ValueError:
        return False, "Port must be a number"


class ESPUploaderGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 LittleFS + NVS Studio")
        self.root.geometry("980x700")

        bundle_dir, app_dir = get_base_dirs()
        self.BUNDLE_DIR = bundle_dir
        self.BASE_DIR = app_dir
        self.CONFIG_FILE = os.path.join(app_dir, "uploaderGUI.json")
        self.DATA_DIR = os.path.normpath(os.path.join(app_dir, "..", "..", "data"))
        if not os.path.isdir(self.DATA_DIR):
            self.DATA_DIR = os.path.join(app_dir, "data")
        self.IMAGE_NAME = os.path.join(app_dir, "littlefs.bin")
        self.WEB_DIR = os.path.join(self.DATA_DIR, "web")
        if not os.path.isdir(self.WEB_DIR):
            local_web = os.path.join(app_dir, "web")
            project_web = os.path.normpath(os.path.join(app_dir, "..", "..", "web"))
            if os.path.isdir(local_web):
                self.WEB_DIR = local_web
            elif os.path.isdir(project_web):
                self.WEB_DIR = project_web
        self.WEB_ALLOWED_EXTS = {".ico", ".css", ".html", ".js"}
        self.WEB_GZIP_EXTS = {".css", ".html", ".js"}

        self.LIB_DIR = os.path.join(app_dir, "lib")
        self.ALT_LIB_DIR = os.path.normpath(os.path.join(app_dir, "..", "..", "lib"))

        binary_name = "mklittlefs.exe" if platform.system() == "Windows" else "mklittlefs"
        self._mklittlefs_candidates = [
            os.path.join(bundle_dir, "mklittlefs", binary_name),
            os.path.join(self.LIB_DIR, "mklittlefs", binary_name),
            os.path.join(self.ALT_LIB_DIR, "mklittlefs", binary_name),
        ]
        self._nvsgen_candidates = [
            os.path.join(self.LIB_DIR, "nvs", "nvs_partition_gen.py"),
            os.path.join(self.ALT_LIB_DIR, "nvs", "nvs_partition_gen.py"),
        ]
        self._csv_candidates = [
            os.path.join(app_dir, "partitions.csv"),
            os.path.normpath(os.path.join(app_dir, "..", "..", "partitions.csv")),
        ]
        self._nvs_csv_candidates = [
            os.path.join(app_dir, "nvs_keys.csv"),
            os.path.normpath(os.path.join(app_dir, "..", "..", "nvs_keys.csv")),
        ]
        self.DEFAULT_MKLITTLEFS_PATH = self._first_existing(self._mklittlefs_candidates)
        self.DEFAULT_NVS_GEN_PY = self._first_existing(self._nvsgen_candidates)
        self.DEFAULT_CSV_PATH = self._first_existing(self._csv_candidates)
        self.DEFAULT_NVS_CSV_PATH = self._first_existing(self._nvs_csv_candidates)

        self._nvs_consistency_issues = []
        self._chip_detect_running = False
        self._progress_var = tk.DoubleVar(value=0)
        self._progress_message_var = tk.StringVar(value="Ready")
        self._progress_window = None
        self._progress_label = None
        self._progress_bar = None

        self.setup_ui()

        import sv_ttk

        sv_ttk.set_theme("dark")
        self.load_config()
        self.refresh_status()
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    @staticmethod
    def _first_existing(paths):
        for p in paths:
            if os.path.isfile(p):
                return p
        return paths[0] if paths else ""

    def setup_ui(self):
        self.tabs = ttk.Notebook(self.root)
        self.config_tab = ttk.Frame(self.tabs)
        self.flash_tab = ttk.Frame(self.tabs)
        self.nvs_tab = ttk.Frame(self.tabs)
        self.terminal_tab = ttk.Frame(self.tabs)
        self.help_tab = ttk.Frame(self.tabs)

        self.tabs.add(self.config_tab, text="  Configuration  ")
        self.tabs.add(self.flash_tab, text="  Flash Operations  ")
        self.tabs.add(self.nvs_tab, text="  NVS Editor  ")
        self.tabs.add(self.terminal_tab, text="  Terminal Output  ")
        self.tabs.add(self.help_tab, text="  System Setup  ")
        self.tabs.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        self._build_config_tab()
        self._build_flash_tab()
        self._build_nvs_tab()
        self._build_terminal_tab()
        self._build_help_tab()

    def _build_config_tab(self):
        container = ttk.Frame(self.config_tab, padding=20)
        container.pack(fill=tk.BOTH, expand=True)

        ttk.Label(container, text="Main Information", font=("Segoe UI", 16, "bold")).pack(anchor=tk.W, pady=(0, 20))

        self.default_port = "/dev/ttyUSB0" if platform.system() == "Linux" else "COM1"
        self.port_var = tk.StringVar(value=self.default_port)
        self.chip_var = tk.StringVar(value="esp32")
        self.csv_path_var = tk.StringVar(value=self.DEFAULT_CSV_PATH)
        self.mklittlefs_path_var = tk.StringVar(value=self.DEFAULT_MKLITTLEFS_PATH)
        self.nvs_gen_py_var = tk.StringVar(value=self.DEFAULT_NVS_GEN_PY)
        self.erase_fs_var = tk.BooleanVar(value=False)
        self.erase_nvs_var = tk.BooleanVar(value=False)

        for var in (self.csv_path_var, self.mklittlefs_path_var, self.nvs_gen_py_var):
            var.trace_add("write", lambda *_args: self.root.after_idle(self.refresh_status))

        grid = ttk.Frame(container)
        grid.pack(fill=tk.X)

        ttk.Label(grid, text=f"Running on: {platform.system()} ({platform.machine()})", font=("Segoe UI", 9, "italic")).grid(
            row=0, column=0, columnspan=3, sticky=tk.W, pady=(0, 10)
        )

        ttk.Label(grid, text="Serial Port").grid(row=1, column=0, sticky=tk.W)
        self.port_combo = ttk.Combobox(grid, textvariable=self.port_var, width=28)
        self.port_combo.grid(row=2, column=0, sticky=tk.W, padx=(0, 20), pady=(5, 10))
        self.port_combo.bind("<<ComboboxSelected>>", lambda _e: self.start_chip_detect_thread())
        self.port_combo.bind("<FocusOut>", lambda _e: self.start_chip_detect_thread())

        ttk.Label(grid, text="Chip Family").grid(row=1, column=1, sticky=tk.W)
        ttk.Combobox(grid, textvariable=self.chip_var, values=list(VALID_CHIPS), width=16, state="readonly").grid(row=2, column=1, sticky=tk.W, pady=(5, 10))

        ttk.Label(grid, text="Partitions Definition (.csv)").grid(row=3, column=0, sticky=tk.W, pady=(10, 4))
        ttk.Entry(grid, textvariable=self.csv_path_var).grid(row=4, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid, text="Browse", command=self.browse_csv).grid(row=4, column=2, padx=8)

        ttk.Label(grid, text="mklittlefs binary path").grid(row=5, column=0, sticky=tk.W, pady=(10, 4))
        ttk.Entry(grid, textvariable=self.mklittlefs_path_var).grid(row=6, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid, text="Browse", command=lambda: self._browse_file(self.mklittlefs_path_var, "Executable", "*")).grid(row=6, column=2, padx=8)

        ttk.Label(grid, text="nvs_partition_gen.py path").grid(row=7, column=0, sticky=tk.W, pady=(10, 4))
        ttk.Entry(grid, textvariable=self.nvs_gen_py_var).grid(row=8, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid, text="Browse", command=lambda: self._browse_file(self.nvs_gen_py_var, "Python", "*.py")).grid(row=8, column=2, padx=8)

        ttk.Checkbutton(grid, text="Erase FS partition before flash", variable=self.erase_fs_var).grid(row=9, column=0, columnspan=2, sticky=tk.W, pady=(14, 0))
        ttk.Checkbutton(grid, text="Erase NVS before write", variable=self.erase_nvs_var).grid(row=10, column=0, columnspan=2, sticky=tk.W, pady=(8, 0))

        grid.columnconfigure(0, weight=1)
        status_row = ttk.Frame(container)
        status_row.pack(fill=tk.X, pady=(18, 0))
        status_row.columnconfigure(0, weight=1)
        status_row.columnconfigure(1, weight=1)

        status_libs = ttk.LabelFrame(status_row, text="Environment Status - Libraries", padding=10)
        status_libs.grid(row=0, column=0, sticky=tk.NSEW, padx=(0, 6))
        status_files = ttk.LabelFrame(status_row, text="Environment Status - Files", padding=10)
        status_files.grid(row=0, column=1, sticky=tk.NSEW, padx=(6, 0))

        self.status_labels = {}
        self.status_labels["mklittlefs"] = ttk.Label(status_libs, text="", font=("Segoe UI", 10))
        self.status_labels["mklittlefs"].pack(anchor=tk.W, pady=2)
        self.status_labels["nvsgen"] = ttk.Label(status_libs, text="", font=("Segoe UI", 10))
        self.status_labels["nvsgen"].pack(anchor=tk.W, pady=2)
        self.status_labels["csv"] = ttk.Label(status_libs, text="", font=("Segoe UI", 10))
        self.status_labels["csv"].pack(anchor=tk.W, pady=2)

        self.status_labels["data"] = ttk.Label(status_files, text="", font=("Segoe UI", 10))
        self.status_labels["data"].pack(anchor=tk.W, pady=2)
        self.status_labels["data_files"] = ttk.Label(status_files, text="", font=("Consolas", 9), foreground="#888888")
        self.status_labels["data_files"].pack(anchor=tk.W, pady=(0, 4), padx=(16, 0))
        self.status_labels["web"] = ttk.Label(status_files, text="", font=("Segoe UI", 10))
        self.status_labels["web"].pack(anchor=tk.W, pady=2)
        self.status_labels["web_files"] = ttk.Label(status_files, text="", font=("Consolas", 9), foreground="#888888")
        self.status_labels["web_files"].pack(anchor=tk.W, pady=(0, 4), padx=(16, 0))

        btns = ttk.Frame(container)
        btns.pack(fill=tk.X, pady=(20, 0))
        ttk.Button(btns, text="Refresh", command=self._on_refresh_click).pack(side=tk.LEFT)
        ttk.Button(btns, text="Save Configuration", command=self.save_config).pack(side=tk.LEFT, padx=(8, 0))
        self.readiness_label = ttk.Label(btns, text="", font=("Segoe UI", 11, "bold"))
        self.readiness_label.pack(side=tk.LEFT, padx=(20, 0))

    @staticmethod
    def _detect_serial_ports():
        if list_ports is None:
            return []
        ports = []
        try:
            ports = sorted((p.device for p in list_ports.comports() if p.device))
        except Exception:
            return []
        return ports

    def refresh_serial_ports(self, announce=False):
        detected = self._detect_serial_ports()
        current = self.port_var.get().strip()
        values = list(detected)
        # Keep manual overrides, but avoid injecting placeholder defaults when ports are detected.
        if current and current not in values and not (detected and current == self.default_port):
            values.insert(0, current)
        self.port_combo["values"] = values
        if detected and (not current or current == self.default_port):
            self.port_var.set(detected[0])
            self.start_chip_detect_thread()
        if announce:
            if detected:
                self.log(f">>> Detected serial ports: {', '.join(detected)}")
            else:
                msg = "No serial ports detected."
                if list_ports is None:
                    msg += " (pyserial not available)"
                self.log(f"[WARN] {msg}")

    def start_chip_detect_thread(self):
        if self._chip_detect_running:
            return
        port = self.port_var.get().strip()
        if not port:
            return
        self._chip_detect_running = True
        threading.Thread(target=self._detect_chip_family, args=(port,), daemon=True).start()

    def _detect_chip_family(self, port):
        try:
            cmd = [sys.executable, "-m", "esptool", "--port", port, "chip_id"]
            result = subprocess.run(cmd, check=False, capture_output=True, text=True, timeout=12)
            output = "\n".join([result.stdout or "", result.stderr or ""])
            detected = self._parse_chip_from_esptool(output)
            if detected and detected in VALID_CHIPS:
                self.root.after(0, self._set_detected_chip, detected, port)
        except Exception:
            pass
        finally:
            self._chip_detect_running = False

    @staticmethod
    def _parse_chip_from_esptool(output):
        patterns = [
            r"Detecting chip type\.\.\.\s*(ESP32[\w\-]*)",
            r"Chip is\s+(ESP32[\w\-]*)",
        ]
        for pat in patterns:
            m = re.search(pat, output, flags=re.IGNORECASE)
            if m:
                return sanitize_chip(m.group(1))
        return ""

    def _set_detected_chip(self, chip, port):
        current_port = self.port_var.get().strip()
        if current_port != port:
            return
        if self.chip_var.get() != chip:
            self.chip_var.set(chip)
            self.log(f">>> Auto-detected chip family on {port}: {chip}")

    def _build_flash_tab(self):
        container = ttk.Frame(self.flash_tab, padding=20)
        container.pack(fill=tk.BOTH, expand=True)

        ttk.Label(container, text="LittleFS Operations", font=("Segoe UI", 16, "bold")).pack(anchor=tk.W, pady=(0, 20))
        ttk.Label(
            container,
            text="Use Configuration tab for COM, paths, and options. This tab only runs the flash operation.",
            font=("Segoe UI", 10),
        ).pack(anchor=tk.W, pady=(0, 12))

        btns = ttk.Frame(container)
        btns.pack(fill=tk.X, pady=(8, 0))
        self.run_btn = tk.Button(
            btns,
            text="Build & Flash LittleFS",
            command=self.start_flash_thread,
            bg="#d64b4b",
            fg="#ffffff",
            activebackground="#be3f3f",
            activeforeground="#ffffff",
            relief=tk.FLAT,
            bd=0,
            padx=12,
            pady=6,
            cursor="hand2",
        )
        self.run_btn.pack(side=tk.LEFT)

    def _ensure_progress_window(self):
        if self._progress_window is not None and self._progress_window.winfo_exists():
            return
        self._progress_window = tk.Toplevel(self.root)
        self._progress_window.title("Operation Progress")
        self._progress_window.geometry("420x82")
        self._progress_window.resizable(False, False)
        self._progress_window.transient(self.root)

        frame = ttk.Frame(self._progress_window, padding=8)
        frame.pack(fill=tk.BOTH, expand=True)

        self._progress_bar = ttk.Progressbar(frame, variable=self._progress_var, maximum=100, mode="determinate")
        self._progress_bar.pack(fill=tk.X)
        self._progress_label = ttk.Label(frame, textvariable=self._progress_message_var, font=("Segoe UI", 9))
        self._progress_label.pack(anchor=tk.W, pady=(4, 0))

        # Hide window instead of destroying it when user closes it.
        self._progress_window.protocol("WM_DELETE_WINDOW", self._progress_window.withdraw)

    def _show_progress_window(self, title="Operation Progress"):
        self._ensure_progress_window()
        self._progress_window.title(title)
        # Center floating progress window on current screen.
        self._progress_window.update_idletasks()
        w = self._progress_window.winfo_width()
        h = self._progress_window.winfo_height()
        sw = self._progress_window.winfo_screenwidth()
        sh = self._progress_window.winfo_screenheight()
        x = max(0, (sw - w) // 2)
        y = max(0, (sh - h) // 2)
        self._progress_window.geometry(f"{w}x{h}+{x}+{y}")
        self._progress_window.deiconify()
        self._progress_window.lift()
        self._progress_window.attributes("-topmost", True)
        self._progress_window.after(150, lambda: self._progress_window.attributes("-topmost", False))

    def _close_progress_window(self, delay_ms=700):
        if self._progress_window is None or not self._progress_window.winfo_exists():
            return
        self._progress_window.after(delay_ms, self._progress_window.withdraw)

    def _set_progress_ui(self, percent: float, message: str):
        pct = max(0.0, min(100.0, float(percent)))
        self._progress_var.set(pct)
        if message:
            self._progress_message_var.set(message)

    def _build_nvs_tab(self):
        container = ttk.Frame(self.nvs_tab, padding=16)
        container.pack(fill=tk.BOTH, expand=True)

        ttk.Label(container, text="NVS Variables", font=("Segoe UI", 15, "bold")).pack(anchor=tk.W, pady=(0, 10))

        top = ttk.Frame(container)
        top.pack(fill=tk.X)
        self.unlock_meta_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            top,
            text="Unlock namespace/key/type/encoding (at your own risk)",
            variable=self.unlock_meta_var,
            command=self._apply_nvs_meta_lock_state,
        ).grid(row=0, column=0, columnspan=6, sticky=tk.W, pady=(0, 8))
        ttk.Label(top, text="Namespace").grid(row=1, column=0, sticky=tk.W)
        ttk.Label(top, text="Key").grid(row=1, column=1, sticky=tk.W)
        ttk.Label(top, text="Type").grid(row=1, column=2, sticky=tk.W)
        ttk.Label(top, text="Encoding").grid(row=1, column=3, sticky=tk.W)
        ttk.Label(top, text="Value").grid(row=1, column=4, sticky=tk.W)

        self.nvs_namespace_var = tk.StringVar(value="storage")
        self.nvs_key_var = tk.StringVar()
        self.nvs_type_var = tk.StringVar(value="data")
        self.nvs_encoding_var = tk.StringVar(value="string")
        self.nvs_value_var = tk.StringVar()

        self.nvs_namespace_entry = ttk.Entry(top, textvariable=self.nvs_namespace_var, width=20)
        self.nvs_namespace_entry.grid(row=2, column=0, sticky=tk.EW, padx=(0, 8))
        self.nvs_key_entry = ttk.Entry(top, textvariable=self.nvs_key_var, width=18)
        self.nvs_key_entry.grid(row=2, column=1, sticky=tk.EW, padx=(0, 8))
        self.nvs_type_combo = ttk.Combobox(top, textvariable=self.nvs_type_var, values=["data", "namespace", "file", "key"], width=10)
        self.nvs_type_combo.grid(row=2, column=2, sticky=tk.EW, padx=(0, 8))
        self.nvs_encoding_combo = ttk.Combobox(
            top,
            textvariable=self.nvs_encoding_var,
            values=["string", "u8", "i8", "u16", "u32", "i32", "base64", "hex2bin", "binary"],
            width=12,
        )
        self.nvs_encoding_combo.grid(row=2, column=3, sticky=tk.EW, padx=(0, 8))
        ttk.Entry(top, textvariable=self.nvs_value_var).grid(row=2, column=4, sticky=tk.EW)
        top.columnconfigure(1, weight=1)
        top.columnconfigure(3, weight=1)
        top.columnconfigure(4, weight=1)
        self._apply_nvs_meta_lock_state()

        tree_frame = ttk.Frame(container)
        tree_frame.pack(fill=tk.BOTH, expand=True, pady=(12, 0))
        self.nvs_tree = ttk.Treeview(tree_frame, columns=("namespace", "key", "type", "encoding", "value"), show="headings", height=13)
        for c, w in [("namespace", 140), ("key", 180), ("type", 90), ("encoding", 110), ("value", 420)]:
            self.nvs_tree.heading(c, text=c.capitalize())
            self.nvs_tree.column(c, width=w, anchor=tk.W)
        tree_scroll = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL, command=self.nvs_tree.yview)
        self.nvs_tree.configure(yscrollcommand=tree_scroll.set)
        self.nvs_tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        tree_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.nvs_tree.bind("<<TreeviewSelect>>", self._on_nvs_select)

        row = ttk.Frame(container)
        row.pack(fill=tk.X, pady=(12, 0))
        ttk.Button(row, text="Add / Update", command=self.add_or_update_nvs).pack(side=tk.LEFT)
        ttk.Button(row, text="Remove Selected", command=self.remove_selected_nvs).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(row, text="Clear", command=self.clear_nvs_form).pack(side=tk.LEFT, padx=(8, 0))
        export_btn = tk.Button(
            row,
            text="Export CSV",
            command=self.export_nvs_csv,
            bg="#2e79d1",
            fg="#ffffff",
            activebackground="#2667b3",
            activeforeground="#ffffff",
            relief=tk.FLAT,
            bd=0,
            padx=10,
            pady=4,
            cursor="hand2",
        )
        export_btn.pack(side=tk.LEFT, padx=(8, 0))
        import_btn = tk.Button(
            row,
            text="Import CSV",
            command=self.import_nvs_csv,
            bg="#2e79d1",
            fg="#ffffff",
            activebackground="#2667b3",
            activeforeground="#ffffff",
            relief=tk.FLAT,
            bd=0,
            padx=10,
            pady=4,
            cursor="hand2",
        )
        import_btn.pack(side=tk.LEFT, padx=(8, 0))

        row_write = ttk.Frame(container)
        row_write.pack(fill=tk.X, pady=(8, 0))
        self.nvs_write_btn = tk.Button(
            row_write,
            text="Write to Device",
            command=self.start_nvs_write_thread,
            bg="#d64b4b",
            fg="#ffffff",
            activebackground="#be3f3f",
            activeforeground="#ffffff",
            relief=tk.FLAT,
            bd=0,
            padx=12,
            pady=4,
            cursor="hand2",
        )
        self.nvs_write_btn.pack(side=tk.LEFT, padx=(8, 0))
        self.nvs_consistency_label = ttk.Label(row_write, text="", font=("Segoe UI", 9))
        self.nvs_consistency_label.pack(side=tk.LEFT, padx=(20, 0))

    def _build_terminal_tab(self):
        frame = ttk.Frame(self.terminal_tab, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)
        self.log_area = tk.Text(frame, bg="#1e1e1e", fg="#ffffff", borderwidth=0, font=("Consolas", 10), wrap=tk.WORD)
        self.log_area.pack(fill=tk.BOTH, expand=True)
        self.log_area.configure(state="disabled")

    def _build_help_tab(self):
        frame = ttk.Frame(self.help_tab, padding=20)
        frame.pack(fill=tk.BOTH, expand=True)

        help_text = tk.Text(frame, wrap=tk.WORD, font=("Segoe UI", 10), bg="#1e1e1e", fg="#ffffff",
                            borderwidth=0, highlightthickness=0, padx=8, pady=8)
        help_text.pack(fill=tk.BOTH, expand=True)

        help_text.tag_configure("link", foreground="#5a9fd4", underline=True)
        help_text.tag_bind("link", "<Enter>", lambda _e: help_text.config(cursor="hand2"))
        help_text.tag_bind("link", "<Leave>", lambda _e: help_text.config(cursor=""))

        def _insert_link(widget, label, url):
            tag = f"link_{url}"
            widget.tag_configure(tag, foreground="#5a9fd4", underline=True)
            widget.tag_bind(tag, "<Enter>", lambda _e: widget.config(cursor="hand2"))
            widget.tag_bind(tag, "<Leave>", lambda _e: widget.config(cursor=""))
            widget.tag_bind(tag, "<Button-1>", lambda _e: webbrowser.open(url))
            widget.insert(tk.END, label, tag)

        help_text.insert(tk.END, "Uploader workflow (current):\n\n")
        help_text.insert(tk.END, "1) Configuration tab\n")
        help_text.insert(tk.END, "- Auto-detected COM port + chip + path to the partition CSV \n")
        help_text.insert(tk.END, "- Install mklittlefs in lib/mklittlefs (from earlephilhower): ")
        _insert_link(help_text, "github.com/earlephilhower/mklittlefs", "https://github.com/earlephilhower/mklittlefs")
        help_text.insert(tk.END, "\n")
        help_text.insert(tk.END, "- Install nvs_partition_gen.py in lib/nvs (from ESP-IDF): ")
        _insert_link(help_text, "github.com/.../nvs_partition_generator", "https://github.com/espressif/esp-idf/blob/master/components/nvs_flash/nvs_partition_generator/")
        help_text.insert(tk.END, "\n")
        help_text.insert(tk.END, "- Optional: enable erase before LittleFS/NVS flashing\n")
        help_text.insert(tk.END, "- Refresh status to validate environment\n\n")
        help_text.insert(tk.END, "2) Flash Operations tab\n")
        help_text.insert(tk.END, "- Builds LittleFS image from /data (all files) and /web (.ico/.css/.html/.js)\n")
        help_text.insert(tk.END, "- Flashes LittleFS at the SPIFFS partition offset from partitions.csv\n\n")
        help_text.insert(tk.END, "3) NVS Editor tab\n")
        help_text.insert(tk.END, "- Import or edit NVS rows in CSV format (key,type,encoding,value)\n")
        help_text.insert(tk.END, "- Auto-imports last CSV saved in uploaderGUI.json when available\n")
        help_text.insert(tk.END, "- Export CSV for backup, then Write to Device to generate+flash NVS\n\n")
        help_text.insert(tk.END, "Required tools:\n")
        help_text.insert(tk.END, "- mklittlefs binary\n")
        help_text.insert(tk.END, "- nvs_partition_gen.py\n")
        help_text.insert(tk.END, "- esp_idf_nvs_partition_gen (Python module)\n")
        help_text.insert(tk.END, "- esptool (Python module)")

        help_text.configure(state="disabled")

    def log(self, message, level="INFO"):
        if threading.current_thread() is not threading.main_thread():
            self.root.after(0, self.log, message, level)
            return
        timestamp = datetime.now().strftime("%H:%M:%S")
        formatted = f"[{timestamp}] [{level}] {message}"
        self.log_area.configure(state="normal")
        self.log_area.insert(tk.END, formatted + "\n")
        self.log_area.see(tk.END)
        self.log_area.configure(state="disabled")

    def update_progress(self, percent: float, message: str = ""):
        """Update progress bar and label."""
        if threading.current_thread() is not threading.main_thread():
            self.root.after(0, self.update_progress, percent, message)
            return
        self._set_progress_ui(percent, message)
        if message:
            self.log(f"[PROGRESS] {message}")
        self.root.update_idletasks()

    def _set_buttons_busy(self, busy):
        if busy:
            self.run_btn.config(state=tk.DISABLED)
            self.nvs_write_btn.config(state=tk.DISABLED)
        else:
            self.refresh_status()

    def _run_python(self, args, check=True):
        cmd = [sys.executable] + args
        self.log(f">>> {' '.join(cmd)}")
        result = subprocess.run(cmd, check=False, capture_output=True, text=True)
        if result.stdout:
            self.log(result.stdout.rstrip())
        if result.stderr:
            self.log(result.stderr.rstrip())
        if check and result.returncode != 0:
            raise subprocess.CalledProcessError(
                result.returncode, cmd, output=result.stdout, stderr=result.stderr
            )
        return result

    def _run_with_fallbacks(self, variants):
        last = None
        for args in variants:
            try:
                return self._run_python(args, check=True)
            except subprocess.CalledProcessError as exc:
                last = exc
                continue
        raise last if last else RuntimeError("No command variant provided")

    def browse_csv(self):
        p = filedialog.askopenfilename(filetypes=[("CSV files", "*.csv")])
        if p:
            self.csv_path_var.set(p)

    def _browse_file(self, tk_var, label, pattern):
        p = filedialog.askopenfilename(filetypes=[(label, pattern), ("All", "*")])
        if p:
            tk_var.set(p)

    def save_config(self):
        chip = sanitize_chip(self.chip_var.get())
        c = {
            "port": self.port_var.get(),
            "chip": chip,
            "csv_path": self.csv_path_var.get(),
            "nvs_csv_path": getattr(self, "last_nvs_csv_path", ""),
            "erase_fs": self.erase_fs_var.get(),
            "erase_nvs": self.erase_nvs_var.get(),
            "mklittlefs_path": self.mklittlefs_path_var.get(),
            "nvs_gen_py": self.nvs_gen_py_var.get(),
        }
        with open(self.CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(c, f, indent=2)

    def load_config(self):
        self.last_nvs_csv_path = self.DEFAULT_NVS_CSV_PATH
        if not os.path.isfile(self.CONFIG_FILE):
            self._auto_import_nvs_csv()
            self.refresh_serial_ports()
            return
        try:
            with open(self.CONFIG_FILE, "r", encoding="utf-8") as f:
                c = json.load(f)
            self.port_var.set(c.get("port", self.port_var.get()))
            raw_chip = c.get("chip", self.chip_var.get())
            chip = sanitize_chip(raw_chip)
            if chip in VALID_CHIPS:
                if chip != raw_chip:
                    self.log(f"[WARN] Corrected chip '{raw_chip}' -> '{chip}'")
                self.chip_var.set(chip)
            else:
                self.log(f"[WARN] Unknown chip '{raw_chip}' in config, keeping default '{self.chip_var.get()}'")
            self.csv_path_var.set(c.get("csv_path", ""))
            self.last_nvs_csv_path = c.get("nvs_csv_path", self.DEFAULT_NVS_CSV_PATH)
            self.erase_fs_var.set(c.get("erase_fs", False))
            self.erase_nvs_var.set(c.get("erase_nvs", False))
            self.mklittlefs_path_var.set(c.get("mklittlefs_path", self.mklittlefs_path_var.get()))
            self.nvs_gen_py_var.set(c.get("nvs_gen_py", self.nvs_gen_py_var.get()))
            self.refresh_serial_ports()
            self._auto_import_nvs_csv()
        except Exception as exc:
            self.log(f"[WARN] Failed to load config: {exc}")

    def _auto_import_nvs_csv(self):
        if self.last_nvs_csv_path and os.path.isfile(self.last_nvs_csv_path):
            self._load_nvs_csv_to_tree(self.last_nvs_csv_path)
            self.log(f">>> Auto-imported NVS CSV: {self.last_nvs_csv_path}")
            self._update_nvs_consistency_label()

    def _on_refresh_click(self):
        """Refresh button handler: re-detect serial ports/chip, then update status."""
        self.refresh_serial_ports()
        self.refresh_status()

    def refresh_status(self):
        ok_color = "#4ec969"
        warn_color = "#e0a820"
        err_color = "#e05555"

        if not os.path.isfile(self.mklittlefs_path_var.get()):
            found = self._first_existing(self._mklittlefs_candidates)
            if os.path.isfile(found):
                self.mklittlefs_path_var.set(found)
        if not os.path.isfile(self.nvs_gen_py_var.get()):
            found = self._first_existing(self._nvsgen_candidates)
            if os.path.isfile(found):
                self.nvs_gen_py_var.set(found)
        if not (self.csv_path_var.get() and os.path.isfile(self.csv_path_var.get())):
            found = self._first_existing(self._csv_candidates)
            if os.path.isfile(found):
                self.csv_path_var.set(found)

        checks = {
            "mklittlefs": os.path.isfile(self.mklittlefs_path_var.get()),
            "nvsgen": os.path.isfile(self.nvs_gen_py_var.get()),
            "web": os.path.isdir(self.WEB_DIR),
            "csv": bool(self.csv_path_var.get() and os.path.isfile(self.csv_path_var.get())),
        }

        data_dir_exists = os.path.isdir(self.DATA_DIR)
        data_files = []
        if data_dir_exists:
            data_files = sorted(
                f
                for f in os.listdir(self.DATA_DIR)
                if os.path.isfile(os.path.join(self.DATA_DIR, f))
            )
        web_files = []
        if checks["web"]:
            web_files = sorted(
                f
                for f in os.listdir(self.WEB_DIR)
                if os.path.isfile(os.path.join(self.WEB_DIR, f))
                and os.path.splitext(f)[1].lower() in self.WEB_ALLOWED_EXTS
            )

        self.status_labels["mklittlefs"].config(
            text=f"mklittlefs: {'Found' if checks['mklittlefs'] else 'Missing'}",
            foreground=ok_color if checks["mklittlefs"] else err_color,
        )
        self.status_labels["nvsgen"].config(
            text=f"nvs_partition_gen.py: {'Found' if checks['nvsgen'] else 'Missing'}",
            foreground=ok_color if checks["nvsgen"] else err_color,
        )
        self.status_labels["csv"].config(
            text=f"partitions.csv: {'Found' if checks['csv'] else 'Not selected'}",
            foreground=ok_color if checks["csv"] else err_color,
        )
        if checks["web"]:
            web_label = "file" if len(web_files) == 1 else "files"
            self.status_labels["web"].config(
                text=f"/web: {len(web_files)} {web_label}",
                foreground=ok_color,
            )
        else:
            self.status_labels["web"].config(text="/web: Missing", foreground=err_color)
        self.status_labels["web_files"].config(text=", ".join(web_files) if web_files else "")
        if not data_dir_exists:
            self.status_labels["data"].config(text="/data: Missing (warning only)", foreground=warn_color)
            self.status_labels["data_files"].config(text="")
        elif not data_files:
            self.status_labels["data"].config(text="/data: 0 files", foreground=warn_color)
            self.status_labels["data_files"].config(text="")
        else:
            data_label = "file" if len(data_files) == 1 else "files"
            self.status_labels["data"].config(text=f"/data: {len(data_files)} {data_label}", foreground=ok_color)
            self.status_labels["data_files"].config(text=", ".join(data_files))

        fs_ready = checks["mklittlefs"] and checks["csv"]
        nvs_ready = checks["nvsgen"] and checks["csv"]
        self.run_btn.config(state=(tk.NORMAL if fs_ready else tk.DISABLED))
        self.nvs_write_btn.config(state=(tk.NORMAL if nvs_ready else tk.DISABLED))

        if not checks["csv"]:
            self.readiness_label.config(text="\u274c Not ready — partitions.csv is required", foreground=err_color)
        elif fs_ready and nvs_ready:
            self.readiness_label.config(text="\u2705 Ready", foreground=ok_color)
        elif fs_ready:
            self.readiness_label.config(text="\u26a0\ufe0f Partially ready (LittleFS only)", foreground=warn_color)
        elif nvs_ready:
            self.readiness_label.config(text="\u26a0\ufe0f Partially ready (NVS only)", foreground=warn_color)
        else:
            self.readiness_label.config(text="\u274c Not ready — partitions.csv is required", foreground=err_color)

        return fs_ready

    @staticmethod
    def parse_partition_table(csv_file):
        out = []
        with open(csv_file, "r", encoding="utf-8") as f:
            reader = csv.reader(row for row in f if not row.lstrip().startswith("#"))
            for row in reader:
                if len(row) < 5:
                    continue
                try:
                    out.append(
                        {
                            "name": row[0].strip(),
                            "type": row[1].strip().lower(),
                            "subtype": row[2].strip().lower(),
                            "offset": int(row[3].strip(), 0),
                            "size": int(row[4].strip(), 0),
                        }
                    )
                except Exception:
                    continue
        return out

    def get_partition(self, subtype):
        csv_path = self.csv_path_var.get()
        if not os.path.isfile(csv_path):
            raise ValueError("Select a valid partition CSV first.")
        for entry in self.parse_partition_table(csv_path):
            if entry["subtype"] == subtype:
                return entry
        raise ValueError(f"Partition subtype '{subtype}' not found in CSV.")

    def start_flash_thread(self):
        if not self.refresh_status():
            self.log("[ERROR] Environment checks are not ready.")
            return
        self.save_config()
        self._show_progress_window("LittleFS Flash Progress")
        self._set_progress_ui(0, "Starting LittleFS operation...")
        self._set_buttons_busy(True)
        threading.Thread(target=self.run_flash, daemon=True).start()

    def run_flash(self):
        try:
            fs = self.get_partition("spiffs")
            self.log(f">>> LittleFS partition offset={hex(fs['offset'])}, size={hex(fs['size'])}")
            self.update_progress(5, "Partition info loaded")

            staging = tempfile.mkdtemp(prefix="littlefs_staging_")
            try:
                # Stage data files
                data_files = []
                if os.path.isdir(self.DATA_DIR):
                    data_files = [f for f in os.listdir(self.DATA_DIR) if os.path.isfile(os.path.join(self.DATA_DIR, f))]
                    total_data = len(data_files)
                    for idx, f in enumerate(data_files):
                        src = os.path.join(self.DATA_DIR, f)
                        shutil.copy2(src, staging)
                        progress = 10 + (idx / total_data) * 20 if total_data > 0 else 30
                        self.update_progress(progress, f"Staging data file {idx + 1}/{total_data}")
                else:
                    self.log("[WARN] data/ folder not found. Building image without data files.")
                    self.update_progress(30, "No data files to stage")

                # Stage web files
                web_staging = os.path.join(staging, "web")
                os.makedirs(web_staging, exist_ok=True)
                web_files = []
                if os.path.isdir(self.WEB_DIR):
                    web_files = sorted(f for f in os.listdir(self.WEB_DIR) if os.path.isfile(os.path.join(self.WEB_DIR, f)))
                    total_web = len(web_files)
                    for idx, f in enumerate(web_files):
                        src = os.path.join(self.WEB_DIR, f)
                        ext = os.path.splitext(f)[1].lower()
                        if ext not in self.WEB_ALLOWED_EXTS:
                            continue
                        shutil.copy2(src, web_staging)
                        if ext in self.WEB_GZIP_EXTS:
                            dst = os.path.join(web_staging, f + ".gz")
                            with open(src, "rb") as fin, gzip.open(dst, "wb") as fout:
                                shutil.copyfileobj(fin, fout)
                        progress = 50 + (idx / total_web) * 20 if total_web > 0 else 70
                        self.update_progress(progress, f"Staging web file {idx + 1}/{total_web}")
                else:
                    self.log("[WARN] web/ folder not found. Building image without web files.")
                    self.update_progress(70, "No web files to stage")

                # Build LittleFS image
                mkl_cmd = [
                    self.mklittlefs_path_var.get(),
                    "-c",
                    staging,
                    "-b",
                    "4096",
                    "-p",
                    "256",
                    "-s",
                    str(fs["size"]),
                    self.IMAGE_NAME,
                ]
                self.log(f">>> {' '.join(mkl_cmd)}")
                self.update_progress(75, "Building LittleFS image...")
                subprocess.run(mkl_cmd, check=True, capture_output=True, text=True)
                self.update_progress(85, "LittleFS image built")
            finally:
                shutil.rmtree(staging, ignore_errors=True)

            # Erase if requested
            if self.erase_fs_var.get():
                self.log(">>> Erasing FS partition...")
                self.update_progress(90, "Erasing FS partition")
                self._run_python([
                    "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", ESPTOOL_BAUD,
                    "erase-region", hex(fs["offset"]), hex(fs["size"]),
                ])

            # Flash LittleFS
            self.log(">>> Flashing LittleFS...")
            self.update_progress(95, "Flashing LittleFS to device")
            self._run_python([
                "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", ESPTOOL_BAUD,
                "write-flash", hex(fs["offset"]), self.IMAGE_NAME,
            ])
            self.log("[SUCCESS] LittleFS flashed successfully.")
            self.update_progress(100, "Flash complete!")
            self.root.after(0, messagebox.showinfo, "Success", "Filesystem flashed successfully.")
        except Exception as e:
            self.log(f"[ERROR] {e}")
            self.root.after(0, messagebox.showerror, "Error", str(e))
        finally:
            if os.path.isfile(self.IMAGE_NAME):
                os.remove(self.IMAGE_NAME)
            self.root.after(0, self._close_progress_window)
            self.root.after(0, self._set_buttons_busy, False)

    def _on_nvs_select(self, _event):
        sel = self.nvs_tree.selection()
        if not sel:
            return
        vals = self.nvs_tree.item(sel[0], "values")
        self.nvs_namespace_var.set(vals[0])
        self.nvs_key_var.set(vals[1])
        self.nvs_type_var.set(vals[2])
        self.nvs_encoding_var.set(vals[3])
        self.nvs_value_var.set(vals[4])

    def _apply_nvs_meta_lock_state(self):
        editable = self.unlock_meta_var.get()
        state = "normal" if editable else "disabled"
        combo_state = "readonly" if editable else "disabled"
        self.nvs_namespace_entry.config(state=state)
        self.nvs_key_entry.config(state=state)
        self.nvs_type_combo.config(state=combo_state)
        self.nvs_encoding_combo.config(state=combo_state)

    def add_or_update_nvs(self):
        row = (
            self.nvs_namespace_var.get().strip(),
            self.nvs_key_var.get().strip(),
            self.nvs_type_var.get().strip() or "data",
            self.nvs_encoding_var.get().strip() or "string",
            self.nvs_value_var.get(),
        )
        if not row[0] or not row[1]:
            messagebox.showwarning("Missing fields", "Namespace and key are required.")
            return

        sel = self.nvs_tree.selection()
        if sel:
            self.nvs_tree.item(sel[0], values=row)
        else:
            self.nvs_tree.insert("", tk.END, values=row)
        self.clear_nvs_form()
        self._update_nvs_consistency_label()

    def remove_selected_nvs(self):
        for i in self.nvs_tree.selection():
            self.nvs_tree.delete(i)
        self._update_nvs_consistency_label()

    def clear_nvs_form(self):
        self.nvs_key_var.set("")
        self.nvs_value_var.set("")
        self.nvs_tree.selection_remove(self.nvs_tree.selection())

    def _tree_rows(self):
        rows = []
        for i in self.nvs_tree.get_children():
            ns, key, typ, enc, val = self.nvs_tree.item(i, "values")
            rows.append({"namespace": ns, "key": key, "type": typ, "encoding": enc, "value": val})
        return rows

    def _write_nvs_csv(self, csv_path):
        rows = self._tree_rows()
        by_ns = {}
        for r in rows:
            by_ns.setdefault(r["namespace"], []).append(r)

        with open(csv_path, "w", encoding="utf-8", newline="") as f:
            w = csv.writer(f)
            w.writerow(["key", "type", "encoding", "value"])
            for ns in sorted(by_ns.keys()):
                w.writerow([ns, "namespace", "", ""])
                for r in by_ns[ns]:
                    w.writerow([r["key"], r["type"], r["encoding"], r["value"]])

    def export_nvs_csv(self):
        p = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV", "*.csv")])
        if not p:
            return
        self._write_nvs_csv(p)
        self.log(f">>> Exported NVS CSV: {p}")

    def import_nvs_csv(self):
        p = filedialog.askopenfilename(filetypes=[("CSV", "*.csv")])
        if not p:
            return
        self.last_nvs_csv_path = p
        self._load_nvs_csv_to_tree(p)
        self.log(f">>> Imported NVS CSV: {p}")
        self._update_nvs_consistency_label()

    def _load_nvs_csv_to_tree(self, csv_path):
        self.nvs_tree.delete(*self.nvs_tree.get_children())
        current_ns = "storage"
        with open(csv_path, "r", encoding="utf-8") as f:
            reader = csv.reader(f)
            for row in reader:
                if not row or row[0] == "key" or row[0].startswith("#"):
                    continue
                key = row[0].strip()
                typ = row[1].strip() if len(row) > 1 else "data"
                enc = row[2].strip() if len(row) > 2 else "string"
                val = row[3].strip() if len(row) > 3 else ""
                if typ == "namespace":
                    current_ns = key
                    continue
                self.nvs_tree.insert("", tk.END, values=(current_ns, key, typ, enc, val))

    # ---- NVS / nvs_keys.h consistency check ----
    # Uses parse_header and compare from utils/nvs-tester/check_nvs_keys.py

    def _find_nvs_keys_h(self):
        """Locate nvs_keys.h relative to the repo root."""
        candidate = os.path.normpath(os.path.join(self.BASE_DIR, "..", "..", "include", "nvs_keys.h"))
        return Path(candidate) if os.path.isfile(candidate) else None

    def _tree_to_ns_keys(self):
        """Build {namespace: {key, ...}} from the current NVS tree."""
        result: dict[str, set[str]] = {}
        for row in self._tree_rows():
            result.setdefault(row["namespace"], set()).add(row["key"])
        return result

    def _check_nvs_consistency(self):
        """Compare tree data against nvs_keys.h. Returns list of issue strings."""
        h_path = self._find_nvs_keys_h()
        if not h_path:
            return []  # header not found, skip silently

        h_data = nvs_parse_header(h_path)
        tree_data = self._tree_to_ns_keys()
        return nvs_compare(h_data, tree_data, "NVS editor")

    def _check_nvs_consistency_enhanced(self):
        """Enhanced consistency check: validates required keys, types, and values."""
        issues = []
        
        # Get required keys from nvs_keys.h
        h_path = self._find_nvs_keys_h()
        if h_path:
            h_data = nvs_parse_header(h_path)
            tree_data = self._tree_to_ns_keys()
            base_issues = nvs_compare(h_data, tree_data, "NVS editor")
            issues.extend(base_issues)
        
        # Additional validation: check required keys per namespace
        required_keys = {
            "gnss": {"rx_pin", "tx_pin", "baud"},
            "wifi": {"ssid", "pass", "dhcp", "ip", "gw", "subnet", "dns", "accesspoint"},
            "ntrip": {"enabled", "host", "port", "mount", "user", "pass"}
        }
        
        tree_data = self._tree_to_ns_keys()
        for ns, keys in required_keys.items():
            present = tree_data.get(ns, set())
            missing = keys - present
            if missing:
                issues.append(f"Namespace '{ns}' missing required keys: {', '.join(sorted(missing))}")
        
        # Validate NVS values
        for row in self._tree_rows():
            key = row["key"]
            value = row["value"]
            encoding = row["encoding"]
            
            # Validate pin numbers
            if key.endswith("_pin"):
                valid, msg = validate_pin_number(value)
                if not valid:
                    issues.append(f"Key '{key}': {msg}")
            
            # Validate baud rates
            if key == "baud":
                valid, msg = validate_baud_rate(value)
                if not valid:
                    issues.append(f"Key 'baud': {msg}")
            
            # Validate IP addresses
            if key in ("ip", "gw", "subnet", "dns"):
                valid, msg = validate_ipv4(value)
                if not valid:
                    issues.append(f"Key '{key}': {msg}")
            
            # Validate port numbers
            if key == "port":
                valid, msg = validate_port(value)
                if not valid:
                    issues.append(f"Key '{key}': {msg}")
            
            # Validate boolean values
            if key in ("enabled", "dhcp", "accesspoint"):
                valid, msg = validate_boolean(value)
                if not valid:
                    issues.append(f"Key '{key}': {msg}")
            
            # Validate timeout/delay values (positive integers)
            if any(suffix in key for suffix in ("_timeout", "_delay", "_ms", "_tries", "_size", "_attempts", "_abandoned", "_valid")):
                valid, msg = validate_positive_int(value)
                if not valid:
                    issues.append(f"Key '{key}': {msg}")
        
        return issues

    def _update_nvs_consistency_label(self):
        """Run the check and update the status label in the NVS tab."""
        issues = self._check_nvs_consistency()
        if issues:
            self.nvs_consistency_label.config(
                text="\u274c NVS keys mismatch with nvs_keys.h (" + str(len(issues)) + " issue" + ("s" if len(issues) != 1 else "") + ")",
                foreground="#e05555",
            )
            self._nvs_consistency_issues = issues
        else:
            h_path = self._find_nvs_keys_h()
            if h_path:
                self.nvs_consistency_label.config(text="\u2705 NVS keys consistent with nvs_keys.h", foreground="#4ec969")
            else:
                self.nvs_consistency_label.config(text="nvs_keys.h not found \u2014 consistency check skipped", foreground="#888888")
            self._nvs_consistency_issues = []

    def start_nvs_write_thread(self):
        issues = self._check_nvs_consistency_enhanced()
        if issues:
            msg = "NVS validation issues:\n\n" + "\n".join(issues) + "\n\nProceed anyway?"
            if not messagebox.askyesno("NVS Validation Warning", msg):
                return
        self._show_progress_window("NVS Write Progress")
        self._set_progress_ui(0, "Starting NVS write...")
        self._set_buttons_busy(True)
        threading.Thread(target=self.write_nvs_to_device, daemon=True).start()

    def write_nvs_to_device(self):
        try:
            nvs = self.get_partition("nvs")
            if not self._tree_rows():
                raise ValueError("NVS table is empty.")

            self.update_progress(10, "Preparing NVS CSV...")
            with tempfile.TemporaryDirectory(prefix="nvs_write_") as tmp:
                csv_path = os.path.join(tmp, "nvs.csv")
                bin_path = os.path.join(tmp, "nvs.bin")
                self._write_nvs_csv(csv_path)

                self.update_progress(30, "Generating NVS binary...")
                self._run_with_fallbacks([
                    [self.nvs_gen_py_var.get(), "generate", csv_path, bin_path, str(nvs["size"])],
                    [self.nvs_gen_py_var.get(), "generate", "--input", csv_path, "--output", bin_path, "--size", str(nvs["size"])],
                ])

                if self.erase_nvs_var.get():
                    self.update_progress(60, "Erasing NVS partition...")
                    self._run_python([
                        "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", ESPTOOL_BAUD,
                        "erase-region", hex(nvs["offset"]), hex(nvs["size"]),
                    ])

                self.update_progress(80, "Flashing NVS to device...")
                self._run_python([
                    "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", ESPTOOL_BAUD,
                    "write-flash", hex(nvs["offset"]), bin_path,
                ])

            self.log("[SUCCESS] NVS partition flashed successfully.")
            self.update_progress(100, "NVS write complete!")
            self.root.after(0, messagebox.showinfo, "Success", "NVS variables written successfully.")
        except Exception as e:
            self.log(f"[ERROR] {e}")
            self.root.after(0, messagebox.showerror, "NVS write failed", str(e))
        finally:
            self.root.after(0, self._close_progress_window)
            self.root.after(0, self._set_buttons_busy, False)

    def on_close(self):
        self.save_config()
        if self._progress_window is not None and self._progress_window.winfo_exists():
            self._progress_window.destroy()
        self.root.destroy()


def check_dependencies():
    missing = []
    for module, pip_name in [("sv_ttk", "sv-ttk"), ("esptool", "esptool"), ("serial", "pyserial")]:
        try:
            __import__(module)
        except ImportError:
            missing.append(pip_name)
    if missing:
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror(
            "Missing dependencies",
            "Install missing packages:\n\n    pip install " + " ".join(missing),
        )
        root.destroy()
        sys.exit(1)


if __name__ == "__main__":
    check_dependencies()
    root = tk.Tk()
    app = ESPUploaderGUI(root)
    root.mainloop()

