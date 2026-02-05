import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import subprocess
import csv
import os
import sys
import json
import threading
import platform


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
        self.root.geometry("850x560")

        # --- Cross-Platform Path Detection ---
        bundle_dir, app_dir = get_base_dirs()
        self.BASE_DIR = app_dir
        self.CONFIG_FILE = os.path.join(app_dir, "littlefs_uploaderGUI.json")
        self.DATA_DIR = os.path.join(app_dir, "data")
        self.IMAGE_NAME = os.path.join(app_dir, "littlefs.bin")

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

        # Partition CSV
        ttk.Label(grid_frame, text="Partitions Definition (.csv)").grid(row=3, column=0, sticky=tk.W, pady=(20, 5))
        self.csv_path_var = tk.StringVar()
        ttk.Entry(grid_frame, textvariable=self.csv_path_var).grid(row=4, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid_frame, text="Browse", style="Accent.TButton", command=self.browse_csv).grid(row=4, column=2, padx=10)

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

    def save_config(self):
        config = {"port": self.port_var.get(), "chip": self.chip_var.get(), "csv_path": self.csv_path_var.get()}
        with open(self.CONFIG_FILE, "w") as f: json.dump(config, f)

    def load_config(self):
        if os.path.exists(self.CONFIG_FILE):
            try:
                with open(self.CONFIG_FILE, "r") as f:
                    c = json.load(f)
                    self.port_var.set(c.get("port", "/dev/ttyUSB0" if platform.system() == "Linux" else "COM8"))
                    self.chip_var.set(c.get("chip", "esp32c3"))
                    self.csv_path_var.set(c.get("csv_path", ""))
            except: pass

    def browse_csv(self):
        p = filedialog.askopenfilename(filetypes=[("CSV files", "*.csv")])
        if p: self.csv_path_var.set(p)

    def get_partition_info(self, csv_file):
        with open(csv_file, mode='r') as f:
            reader = csv.reader(row for row in f if not row.startswith('#'))
            for row in reader:
                if len(row) >= 5 and 'spiffs' in row[2].strip().lower():
                    return int(row[3].strip(), 16), int(row[4].strip(), 16)
        raise ValueError("Could not find 'spiffs' partition in CSV.")

    def run_process(self):
        self.root.after(0, self._switch_to_terminal)
        self.run_btn.pack_forget()
        if not self.refresh_status():
            self.log("[ERROR] Environment check failed. See status above.")
            return
        self.save_config()
        try:
            csv_path = self.csv_path_var.get()
            
            offset, fs_size = self.get_partition_info(csv_path)
            self.log(f">>> Found Partition: Offset {hex(offset)}, Size {fs_size}")

            # 1. Build Image
            self.log(">>> Building LittleFS Image...")
            build_cmd = [self.MKLITTLEFS_PATH, "-c", self.DATA_DIR, "-b", "4096", "-p", "256", "-s", str(fs_size), self.IMAGE_NAME]
            subprocess.run(build_cmd, check=True, capture_output=True)

            # 2. Flash Image
            self.log(f">>> Flashing to {self.port_var.get()}...")
            # Use 'python3' on Linux, 'python' on Windows
            py_cmd = "python3" if platform.system() == "Linux" else "python"
            flash_cmd = [py_cmd, "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", "921600", "write_flash", hex(offset), self.IMAGE_NAME]
            
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
    for module, pip_name in [("sv_ttk", "sv-ttk"), ("esptool", "esptool")]:
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