import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import subprocess
import csv
import os
import sys
import json
import threading
import platform
import shutil
import tempfile
import gzip


def get_base_dirs():
    """Return (bundle_dir, app_dir).
    bundle_dir: where bundled assets live (mklittlefs, sv_ttk).
    app_dir:    where user files live (data/, config.json) — always next to the .exe or .py.
    """
    if getattr(sys, 'frozen', False):
        bundle_dir = sys._MEIPASS
        app_dir = os.path.dirname(sys.executable)
    else:
        bundle_dir = os.path.dirname(os.path.abspath(__file__))
        app_dir = bundle_dir
    return bundle_dir, app_dir


class CrossPlatformFlasher:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 LittleFS Studio")
        self.root.geometry("850x650")

        # --- Cross-Platform Path Detection ---
        bundle_dir, app_dir = get_base_dirs()
        self.BASE_DIR = app_dir
        self.CONFIG_FILE = os.path.join(app_dir, "littlefs_uploaderGUI.json")
        self.DATA_DIR = os.path.join(app_dir, "data")
        self.IMAGE_NAME = os.path.join(app_dir, "littlefs.bin")

        # Web assets to include in the LittleFS image under /web/
        self.WEB_FILES = ["app.js", "favicon.ico", "index.html", "style.css"]
        self.WEB_DIR = os.path.join(app_dir, "web")
        if not os.path.isdir(self.WEB_DIR):
            project_web = os.path.normpath(os.path.join(app_dir, "..", "..", "web"))
            if os.path.isdir(project_web):
                self.WEB_DIR = project_web

        # Determine mklittlefs binary name based on OS
        binary_name = "mklittlefs.exe" if platform.system() == "Windows" else "mklittlefs"
        self.MKLITTLEFS_PATH = os.path.join(bundle_dir, "mklittlefs", binary_name)

        self.setup_ui()
        import sv_ttk
        sv_ttk.set_theme("dark")
        self._apply_dark_titlebar()
        theme_bg = ttk.Style().lookup("TFrame", "background") or self.root.cget("bg")
        self.status_dot.configure(bg=theme_bg)

        self.load_config()
        self.refresh_status()

    def setup_ui(self):
        self.tabs = ttk.Notebook(self.root)
        self.flash_tab = ttk.Frame(self.tabs)
        self.terminal_tab = ttk.Frame(self.tabs)
        self.help_tab = ttk.Frame(self.tabs)

        self.tabs.add(self.flash_tab, text="  Flash Operations  ")
        self.tabs.add(self.terminal_tab, text="  Terminal Output  ")
        self.tabs.add(self.help_tab, text="  System Setup  ")
        self.tabs.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        self._build_flash_tab()
        self._build_terminal_tab()
        self._build_help_tab()

    def _build_flash_tab(self):
        container = ttk.Frame(self.flash_tab, padding=20)
        container.pack(fill=tk.BOTH, expand=True)

        # Header
        header = ttk.Label(container, text="Flash Configuration", font=("Segoe UI", 16, "bold"))
        header.pack(anchor=tk.W, pady=(0, 20))

        grid_frame = ttk.Frame(container)
        grid_frame.pack(fill=tk.X)

        # OS Indicator
        os_info = f"Running on: {platform.system()} ({platform.machine()})"
        ttk.Label(grid_frame, text=os_info, font=("Segoe UI", 9, "italic")).grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 10))

        # Port & Chip
        ttk.Label(grid_frame, text="Serial Port (e.g. COM8 or /dev/ttyUSB0)").grid(row=1, column=0, sticky=tk.W, pady=5)
        self.port_var = tk.StringVar(value="/dev/ttyUSB0" if platform.system() == "Linux" else "COM8")
        ttk.Entry(grid_frame, textvariable=self.port_var, width=25).grid(row=2, column=0, sticky=tk.W, padx=(0, 20))

        ttk.Label(grid_frame, text="Chip Family").grid(row=1, column=1, sticky=tk.W, pady=5)
        self.chip_var = tk.StringVar(value="esp32c3")
        chips = ["esp32", "esp32s2", "esp32s3", "esp32c2", "esp32c3", "esp32c6", "esp32h2", "esp32p4", "esp8266"]
        self.chip_combo = ttk.Combobox(grid_frame, textvariable=self.chip_var, values=chips, width=15)
        self.chip_combo.grid(row=2, column=1, sticky=tk.W)

        # Gzip web assets
        self.gzip_web_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(grid_frame, text="Gzip web assets (html, css, js)",
                        variable=self.gzip_web_var).grid(row=3, column=0, columnspan=2, sticky=tk.W, pady=(15, 0))

        # Optional pre-erase of the FS partition
        self.erase_fs_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(grid_frame, text="Erase FS partition before flash",
                        variable=self.erase_fs_var).grid(row=4, column=0, columnspan=2, sticky=tk.W, pady=(8, 0))

        # Partition CSV
        ttk.Label(grid_frame, text="Partitions Definition (.csv)").grid(row=5, column=0, sticky=tk.W, pady=(20, 5))
        self.csv_path_var = tk.StringVar()
        ttk.Entry(grid_frame, textvariable=self.csv_path_var).grid(row=6, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid_frame, text="Browse", style="Accent.TButton", command=self.browse_csv).grid(row=6, column=2, padx=10)

        grid_frame.columnconfigure(0, weight=1)

        # Status Info Box
        status_frame = ttk.LabelFrame(container, text="Environment Status", padding=10)
        status_frame.pack(fill=tk.X, pady=(20, 0))

        # Overall status dot
        overall_frame = ttk.Frame(status_frame)
        overall_frame.pack(anchor=tk.W, pady=(0, 8))

        self.status_dot = tk.Canvas(overall_frame, width=14, height=14, highlightthickness=0, borderwidth=0)
        self.status_dot.pack(side=tk.LEFT, padx=(0, 6))
        self.status_dot_id = self.status_dot.create_oval(2, 2, 12, 12, fill="#e05555", outline="")

        self.overall_status_label = ttk.Label(overall_frame, text="Not Ready", font=("Segoe UI", 10, "bold"))
        self.overall_status_label.pack(side=tk.LEFT)

        self.mklittlefs_status = ttk.Label(status_frame, font=("Segoe UI", 10))
        self.mklittlefs_status.pack(anchor=tk.W)

        self.data_status = ttk.Label(status_frame, font=("Segoe UI", 10))
        self.data_status.pack(anchor=tk.W, pady=(4, 0))

        self.data_files_label = ttk.Label(status_frame, font=("Consolas", 9), foreground="#888888")
        self.data_files_label.pack(anchor=tk.W, padx=(16, 0))

        self.web_status = ttk.Label(status_frame, font=("Segoe UI", 10))
        self.web_status.pack(anchor=tk.W, pady=(4, 0))

        self.web_files_label = ttk.Label(status_frame, font=("Consolas", 9), foreground="#888888")
        self.web_files_label.pack(anchor=tk.W, padx=(16, 0))

        self.csv_status = ttk.Label(status_frame, font=("Segoe UI", 10))
        self.csv_status.pack(anchor=tk.W, pady=(4, 0))

        ttk.Button(status_frame, text="Refresh", command=self.refresh_status).place(relx=1.0, y=0, anchor=tk.NE)

        # Action Buttons
        btn_frame = ttk.Frame(container)
        btn_frame.pack(fill=tk.X, pady=(20, 0))

        ttk.Button(btn_frame, text="Show Terminal", command=self._switch_to_terminal).pack(side=tk.LEFT)

        self.run_btn = tk.Button(btn_frame, text="  Build & Flash  ", command=self.start_process_thread,
                                  bg="#e05555", fg="#ffffff", activebackground="#c04040", activeforeground="#ffffff",
                                  font=("Segoe UI", 11, "bold"), relief=tk.FLAT, cursor="hand2",
                                  disabledforeground="#888888")
        self.run_btn.pack(side=tk.RIGHT)

        self.refresh_status()

    def _build_terminal_tab(self):
        container = ttk.Frame(self.terminal_tab, padding=20)
        container.pack(fill=tk.BOTH, expand=True)

        self.log_area = tk.Text(container, background="#1e1e1e", foreground="#ffffff",
                                borderwidth=0, font=("Consolas", 10), padx=10, pady=10, wrap=tk.WORD)
        self.log_area.pack(fill=tk.BOTH, expand=True)

    def _build_help_tab(self):
        container = ttk.Frame(self.help_tab)
        container.pack(fill=tk.BOTH, expand=True)

        canvas = tk.Canvas(container, highlightthickness=0)
        scrollbar = ttk.Scrollbar(container, orient=tk.VERTICAL, command=canvas.yview)
        content = ttk.Frame(canvas, padding=30)

        content.bind("<Configure>", lambda _: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=content, anchor=tk.NW)
        canvas.configure(yscrollcommand=scrollbar.set)

        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

        # Title
        ttk.Label(content, text="System Setup Guide", font=("Segoe UI", 16, "bold")).pack(anchor=tk.W, pady=(0, 20))

        steps = [
            (
                "Step 1 — Install Python Dependencies",
                "Open a terminal and run:\n\n"
                "    pip install esptool sv-ttk\n\n"
                "This installs the ESP flashing tool and the UI theme.\n"
                "If running from the standalone .exe, skip this step."
            ),
            (
                "Step 2 — Place mklittlefs Binary",
                "The tool expects the mklittlefs executable at:\n\n"
                f"    {self.MKLITTLEFS_PATH}\n\n"
                "Download it from the mklittlefs GitHub releases page\n"
                "and place it in the mklittlefs/ folder next to this app.\n"
                "On Windows: mklittlefs.exe + libwinpthread-1.dll\n"
                "On Linux: mklittlefs (ensure it is executable)"
            ),
            (
                "Step 3 — Populate the data/ Folder",
                "Place all the files you want on the ESP32 filesystem\n"
                "into the data/ folder next to this app.\n\n"
                "Common files: wifi.json, gnss.json, ntrip_config.json, web assets, etc.\n\n"
                "The Environment Status panel on the Flash tab shows\n"
                "which files are currently detected."
            ),
            (
                "Step 4 — Select a Partitions CSV",
                "Use the Browse button on the Flash tab to select your\n"
                "partition table CSV file.\n\n"
                "The CSV must contain a row with subtype 'spiffs'.\n"
                "The tool reads the offset and size from that row to\n"
                "know where to flash the filesystem image.\n\n"
                "Example CSV row:\n"
                "    data, data, spiffs, 0x310000, 0xF0000"
            ),
            (
                "Step 5 — Connect and Flash",
                "1. Connect your ESP32 via USB\n"
                "2. Select the correct serial port (e.g. COM8, /dev/ttyUSB0)\n"
                "3. Select your chip family (esp32, esp32c3, etc.)\n"
                "4. Verify all Environment Status indicators are green\n"
                "5. Click Build & Flash\n\n"
                "The tool builds a LittleFS image from data/ and flashes\n"
                "it to the device at the partition offset from the CSV."
            ),
        ]

        if platform.system() == "Linux":
            steps.append((
                "Linux — Serial Port Permissions",
                "If you get a permission error on the serial port,\n"
                "add your user to the dialout group:\n\n"
                "    sudo usermod -a -G dialout $USER\n\n"
                "Then log out and back in for it to take effect."
            ))

        for title, body in steps:
            step_frame = ttk.LabelFrame(content, text=title, padding=12)
            step_frame.pack(fill=tk.X, pady=(0, 12))
            ttk.Label(step_frame, text=body, font=("Consolas", 10), justify=tk.LEFT).pack(anchor=tk.W)

    # --- Logic ---
    def _show_dialog(self, title, message, color):
        dlg = tk.Toplevel(self.root)
        dlg.title(title)
        dlg.resizable(False, False)
        dlg.grab_set()

        frame = ttk.Frame(dlg, padding=30)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(frame, text=title, font=("Segoe UI", 13, "bold"),
                  foreground=color).pack(anchor=tk.W)
        ttk.Label(frame, text=message, font=("Segoe UI", 10),
                  wraplength=380, justify=tk.LEFT).pack(anchor=tk.W, pady=(12, 20))
        ttk.Button(frame, text="OK", style="Accent.TButton",
                   command=dlg.destroy).pack(anchor=tk.E)

        dlg.update_idletasks()
        x = self.root.winfo_x() + (self.root.winfo_width() - dlg.winfo_width()) // 2
        y = self.root.winfo_y() + (self.root.winfo_height() - dlg.winfo_height()) // 2
        dlg.geometry(f"+{x}+{y}")

        if platform.system() == "Windows":
            try:
                import ctypes
                hwnd = ctypes.windll.user32.GetParent(dlg.winfo_id())
                value = ctypes.c_int(1)
                for attr in (20, 19):
                    if ctypes.windll.dwmapi.DwmSetWindowAttribute(
                        hwnd, attr, ctypes.byref(value), ctypes.sizeof(value)
                    ) == 0:
                        break
            except Exception:
                pass

        dlg.wait_window()

    def _apply_dark_titlebar(self):
        if platform.system() != "Windows":
            return
        try:
            import ctypes
            self.root.update()
            hwnd = ctypes.windll.user32.GetParent(self.root.winfo_id())
            value = ctypes.c_int(1)
            # Attribute 20 for Windows 11+, fall back to 19 for Windows 10
            for attr in (20, 19):
                if ctypes.windll.dwmapi.DwmSetWindowAttribute(
                    hwnd, attr, ctypes.byref(value), ctypes.sizeof(value)
                ) == 0:
                    break
            self.root.withdraw()
            self.root.deiconify()
        except Exception:
            pass

    def _switch_to_terminal(self):
        self.tabs.select(self.terminal_tab)

    def refresh_status(self):
        ready = True

        # mklittlefs check
        if os.path.isfile(self.MKLITTLEFS_PATH):
            self.mklittlefs_status.config(text="mklittlefs: Found", foreground="#4ec969")
        else:
            self.mklittlefs_status.config(text="mklittlefs: Not found", foreground="#e05555")
            ready = False

        # data directory check
        if os.path.isdir(self.DATA_DIR):
            files = os.listdir(self.DATA_DIR)
            if files:
                self.data_status.config(text=f"data/: {len(files)} file(s)", foreground="#4ec969")
                listing = ", ".join(sorted(files))
                self.data_files_label.config(text=listing)
            else:
                self.data_status.config(text="data/: Empty", foreground="#e0a820")
                self.data_files_label.config(text="")
                ready = False
        else:
            self.data_status.config(text="data/: Directory not found", foreground="#e05555")
            self.data_files_label.config(text="")
            ready = False

        # web files check
        if os.path.isdir(self.WEB_DIR):
            found = [f for f in self.WEB_FILES if os.path.isfile(os.path.join(self.WEB_DIR, f))]
            missing = [f for f in self.WEB_FILES if f not in found]
            if not missing:
                self.web_status.config(text=f"web/: {len(found)} file(s)", foreground="#4ec969")
                self.web_files_label.config(text=", ".join(sorted(found)))
            else:
                self.web_status.config(text=f"web/: Missing {', '.join(missing)}", foreground="#e0a820")
                self.web_files_label.config(text=f"Found: {', '.join(sorted(found))}" if found else "")
                ready = False
        else:
            self.web_status.config(text="web/: Directory not found", foreground="#e05555")
            self.web_files_label.config(text="")
            ready = False

        # CSV file check
        csv_path = self.csv_path_var.get()
        if csv_path and os.path.isfile(csv_path):
            self.csv_status.config(text=f"Partitions CSV: Found", foreground="#4ec969")
        else:
            self.csv_status.config(text="Partitions CSV: Not selected" if not csv_path else "Partitions CSV: File not found", foreground="#e05555")
            ready = False

        # Update overall status dot
        color = "#4ec969" if ready else "#e05555"
        self.status_dot.itemconfig(self.status_dot_id, fill=color)
        self.overall_status_label.config(text="Ready" if ready else "Not Ready", foreground=color)

        if ready:
            self.run_btn.pack(side=tk.RIGHT)
        else:
            self.run_btn.pack_forget()

        return ready

    def log(self, message):
        self.log_area.configure(state='normal')
        self.log_area.insert(tk.END, message + "\n")
        self.log_area.see(tk.END)
        self.log_area.configure(state='disabled')

    def log_staged_files_table(self, staging_dir):
        def _fmt_ko_mo(size_bytes):
            ko = size_bytes / 1024.0
            mo = size_bytes / (1024.0 * 1024.0)
            return f"{ko:.2f} Ko / {mo:.2f} Mo"

        rows = []
        total = 0
        for root, _, files in os.walk(staging_dir):
            for name in sorted(files):
                full_path = os.path.join(root, name)
                rel_path = os.path.relpath(full_path, staging_dir).replace("\\", "/")
                size = os.path.getsize(full_path)
                rows.append((rel_path, size))
                total += size

        self.log(">>> Final staged files (to be uploaded)")
        if not rows:
            self.log("    (none)")
            return

        name_w = max(len("Path"), max(len(r[0]) for r in rows))
        size_b_w = max(len("Size (bytes)"), max(len(str(r[1])) for r in rows))
        size_hr_w = max(len("Size (Ko / Mo)"), max(len(_fmt_ko_mo(r[1])) for r in rows))
        sep = f"    +{'-' * (name_w + 2)}+{'-' * (size_b_w + 2)}+{'-' * (size_hr_w + 2)}+"

        self.log(sep)
        self.log(
            f"    | {'Path'.ljust(name_w)} | "
            f"{'Size (bytes)'.rjust(size_b_w)} | "
            f"{'Size (Ko / Mo)'.ljust(size_hr_w)} |"
        )
        self.log(sep)
        for rel_path, size in rows:
            self.log(
                f"    | {rel_path.ljust(name_w)} | "
                f"{str(size).rjust(size_b_w)} | "
                f"{_fmt_ko_mo(size).ljust(size_hr_w)} |"
            )
        self.log(sep)
        self.log(
            f"    | {'TOTAL'.ljust(name_w)} | "
            f"{str(total).rjust(size_b_w)} | "
            f"{_fmt_ko_mo(total).ljust(size_hr_w)} |"
        )
        self.log(sep)
        self.log(f">>> Staged total size: {total} bytes ({_fmt_ko_mo(total)})")

    def save_config(self):
        config = {
            "port": self.port_var.get(),
            "chip": self.chip_var.get(),
            "csv_path": self.csv_path_var.get(),
            "gzip_web": self.gzip_web_var.get(),
            "erase_fs": self.erase_fs_var.get(),
        }
        with open(self.CONFIG_FILE, "w") as f: json.dump(config, f)

    def load_config(self):
        if os.path.exists(self.CONFIG_FILE):
            try:
                with open(self.CONFIG_FILE, "r") as f:
                    c = json.load(f)
                    self.port_var.set(c.get("port", "/dev/ttyUSB0" if platform.system() == "Linux" else "COM8"))
                    self.chip_var.set(c.get("chip", "esp32c3"))
                    self.csv_path_var.set(c.get("csv_path", ""))
                    self.gzip_web_var.set(c.get("gzip_web", True))
                    self.erase_fs_var.set(c.get("erase_fs", False))
            except: pass

    def browse_csv(self):
        p = filedialog.askopenfilename(filetypes=[("CSV files", "*.csv")])
        if p: self.csv_path_var.set(p)

    def parse_partition_table(self, csv_file):
        entries = []
        with open(csv_file, mode='r') as f:
            reader = csv.reader(row for row in f if not row.lstrip().startswith('#'))
            for row in reader:
                if len(row) < 5:
                    continue
                name = row[0].strip()
                ptype = row[1].strip().lower()
                subtype = row[2].strip().lower()
                offset_raw = row[3].strip()
                size_raw = row[4].strip()
                if not name or not offset_raw or not size_raw:
                    continue
                try:
                    offset = int(offset_raw, 0)
                    size = int(size_raw, 0)
                except ValueError:
                    continue
                entries.append({
                    "name": name,
                    "type": ptype,
                    "subtype": subtype,
                    "offset": offset,
                    "size": size,
                })
        return entries

    def get_partition_info(self, csv_file):
        entries = self.parse_partition_table(csv_file)
        for entry in entries:
            if entry["subtype"] == "spiffs":
                return entry["offset"], entry["size"], entries
        raise ValueError("Could not find 'spiffs' partition in CSV.")

    def log_partition_table(self, entries):
        def _fmt_ko_mo(size_bytes):
            ko = size_bytes / 1024.0
            mo = size_bytes / (1024.0 * 1024.0)
            return f"{ko:.2f} Ko / {mo:.2f} Mo"

        if not entries:
            self.log(">>> Partitions: none found")
            return

        self.log(">>> Partition table")
        name_w = max(len("Name"), max(len(e["name"]) for e in entries))
        type_w = max(len("Type"), max(len(e["type"]) for e in entries))
        subtype_w = max(len("Subtype"), max(len(e["subtype"]) for e in entries))
        offset_w = max(len("Offset"), max(len(hex(e["offset"])) for e in entries))
        size_b_w = max(len("Size (bytes)"), max(len(str(e["size"])) for e in entries))
        size_hr_w = max(len("Size (Ko / Mo)"), max(len(_fmt_ko_mo(e["size"])) for e in entries))

        sep = (
            f"    +{'-' * (name_w + 2)}+{'-' * (type_w + 2)}+{'-' * (subtype_w + 2)}+"
            f"{'-' * (offset_w + 2)}+{'-' * (size_b_w + 2)}+{'-' * (size_hr_w + 2)}+"
        )
        self.log(sep)
        self.log(
            f"    | {'Name'.ljust(name_w)} | {'Type'.ljust(type_w)} | {'Subtype'.ljust(subtype_w)} | "
            f"{'Offset'.rjust(offset_w)} | {'Size (bytes)'.rjust(size_b_w)} | {'Size (Ko / Mo)'.ljust(size_hr_w)} |"
        )
        self.log(sep)
        for e in entries:
            self.log(
                f"    | {e['name'].ljust(name_w)} | {e['type'].ljust(type_w)} | {e['subtype'].ljust(subtype_w)} | "
                f"{hex(e['offset']).rjust(offset_w)} | {str(e['size']).rjust(size_b_w)} | {_fmt_ko_mo(e['size']).ljust(size_hr_w)} |"
            )
        self.log(sep)
        total_size = sum(e["size"] for e in entries)
        self.log(
            f"    | {'TOTAL'.ljust(name_w)} | {'-'.ljust(type_w)} | {'-'.ljust(subtype_w)} | "
            f"{'-'.rjust(offset_w)} | {str(total_size).rjust(size_b_w)} | {_fmt_ko_mo(total_size).ljust(size_hr_w)} |"
        )
        self.log(sep)

    def run_process(self):
        self.root.after(0, self._switch_to_terminal)
        self.run_btn.pack_forget()
        if not self.refresh_status():
            self.log("[ERROR] Environment check failed. See status above.")
            return
        self.save_config()
        try:
            csv_path = self.csv_path_var.get()
            
            offset, fs_size, partitions = self.get_partition_info(csv_path)
            self.log_partition_table(partitions)
            self.log(f">>> Found Partition: Offset {hex(offset)}, Size {fs_size}")

            # 1. Build staging directory (data/ at root + web/ subfolder)
            staging_dir = tempfile.mkdtemp(prefix="littlefs_staging_")
            try:
                for f in os.listdir(self.DATA_DIR):
                    src = os.path.join(self.DATA_DIR, f)
                    if os.path.isfile(src):
                        shutil.copy2(src, staging_dir)

                web_staging = os.path.join(staging_dir, "web")
                os.makedirs(web_staging)
                gzip_enabled = self.gzip_web_var.get()
                gzip_exts = {".html", ".css", ".js", ".ico"}
                for f in self.WEB_FILES:
                    src = os.path.join(self.WEB_DIR, f)
                    if not os.path.isfile(src):
                        continue
                    _, ext = os.path.splitext(f)
                    if gzip_enabled and ext in gzip_exts:
                        dst = os.path.join(web_staging, f + ".gz")
                        with open(src, "rb") as fin, gzip.open(dst, "wb") as fout:
                            shutil.copyfileobj(fin, fout)
                        orig = os.path.getsize(src)
                        comp = os.path.getsize(dst)
                        self.log(f"    gzip {f}: {orig} -> {comp} bytes ({100 - comp * 100 // orig}% saved)")
                    else:
                        shutil.copy2(src, web_staging)

                self.log_staged_files_table(staging_dir)
                self.log(">>> Building LittleFS Image...")
                build_cmd = [self.MKLITTLEFS_PATH, "-c", staging_dir, "-b", "4096", "-p", "256", "-s", str(fs_size), self.IMAGE_NAME]
                subprocess.run(build_cmd, check=True, capture_output=True)
            finally:
                shutil.rmtree(staging_dir, ignore_errors=True)

            # 2. Flash Image
            self.log(f">>> Flashing to {self.port_var.get()}...")
            # Use 'python3' on Linux, 'python' on Windows
            py_cmd = "python3" if platform.system() == "Linux" else "python"
            if self.erase_fs_var.get():
                self.log(f">>> Erasing FS partition: offset={hex(offset)}, size={hex(fs_size)}")
                erase_cmd = [
                    py_cmd, "-m", "esptool",
                    "--chip", self.chip_var.get(),
                    "--port", self.port_var.get(),
                    "--baud", "921600",
                    "erase-region", hex(offset), hex(fs_size)
                ]
                erase_result = subprocess.run(erase_cmd, check=True, capture_output=True, text=True)
                if erase_result.stdout:
                    self.log(erase_result.stdout)

            flash_cmd = [py_cmd, "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", "921600", "write-flash", hex(offset), self.IMAGE_NAME]
            
            result = subprocess.run(flash_cmd, check=True, capture_output=True, text=True)
            self.log(result.stdout)
            self.log(">>> SUCCESS!")
            self.root.after(0, lambda: self._show_dialog("Success", "Filesystem flashed successfully!", "#4ec969"))
        except Exception as e:
            self.log(f"\n[ERROR] {str(e)}")
            self.root.after(0, lambda msg=str(e): self._show_dialog("Error", msg, "#e05555"))
        finally:
            if os.path.exists(self.IMAGE_NAME): os.remove(self.IMAGE_NAME)
            self.refresh_status()

    def start_process_thread(self):
        threading.Thread(target=self.run_process, daemon=True).start()

def check_dependencies():
    missing = []
    for module, pip_name in [("sv_ttk", "sv-ttk"), ("esptool", "esp-tool")]:
        try:
            __import__(module)
        except ImportError:
            missing.append(pip_name)
    if missing:
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror(
            "Missing Dependencies",
            f"The following Python packages are not installed:\n\n"
            f"    {', '.join(missing)}\n\n"
            f"Install them with:\n\n"
            f"    pip install {' '.join(missing)}"
        )
        root.destroy()
        sys.exit(1)


if __name__ == "__main__":
    check_dependencies()
    root = tk.Tk()
    app = CrossPlatformFlasher(root)
    root.mainloop()
