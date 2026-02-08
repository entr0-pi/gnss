import csv
import gzip
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk


def get_base_dirs():
    """Return (bundle_dir, app_dir)."""
    if getattr(sys, "frozen", False):
        bundle_dir = sys._MEIPASS
        app_dir = os.path.dirname(sys.executable)
    else:
        bundle_dir = os.path.dirname(os.path.abspath(__file__))
        app_dir = bundle_dir
    return bundle_dir, app_dir


class ESPUploaderGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 LittleFS + NVS Studio")
        self.root.geometry("980x730")

        bundle_dir, app_dir = get_base_dirs()
        self.BUNDLE_DIR = bundle_dir
        self.BASE_DIR = app_dir
        self.CONFIG_FILE = os.path.join(app_dir, "uploaderGUI.json")
        self.DATA_DIR = os.path.join(app_dir, "data")
        self.IMAGE_NAME = os.path.join(app_dir, "littlefs.bin")

        self.LIB_DIR = os.path.join(app_dir, "lib")
        self.ALT_LIB_DIR = os.path.normpath(os.path.join(app_dir, "..", "..", "lib"))

        self.DEFAULT_MKLITTLEFS_PY = self._first_existing([
            os.path.join(self.LIB_DIR, "mklittlefs.py"),
            os.path.join(self.ALT_LIB_DIR, "mklittlefs.py"),
        ])
        self.DEFAULT_NVS_GEN_PY = self._first_existing([
            os.path.join(self.LIB_DIR, "nvs_partition_gen.py"),
            os.path.join(self.ALT_LIB_DIR, "nvs_partition_gen.py"),
        ])

        self.WEB_FILES = ["app.js", "favicon.ico", "index.html", "style.css"]
        self.WEB_DIR = os.path.join(app_dir, "web")
        if not os.path.isdir(self.WEB_DIR):
            project_web = os.path.normpath(os.path.join(app_dir, "..", "..", "web"))
            if os.path.isdir(project_web):
                self.WEB_DIR = project_web

        self.setup_ui()

        import sv_ttk

        sv_ttk.set_theme("dark")
        self.load_config()
        self.refresh_status()

    @staticmethod
    def _first_existing(paths):
        for p in paths:
            if os.path.isfile(p):
                return p
        return paths[0] if paths else ""

    def setup_ui(self):
        self.tabs = ttk.Notebook(self.root)
        self.flash_tab = ttk.Frame(self.tabs)
        self.nvs_tab = ttk.Frame(self.tabs)
        self.terminal_tab = ttk.Frame(self.tabs)
        self.help_tab = ttk.Frame(self.tabs)

        self.tabs.add(self.flash_tab, text="  Flash Operations  ")
        self.tabs.add(self.nvs_tab, text="  NVS Editor  ")
        self.tabs.add(self.terminal_tab, text="  Terminal Output  ")
        self.tabs.add(self.help_tab, text="  System Setup  ")
        self.tabs.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        self._build_flash_tab()
        self._build_nvs_tab()
        self._build_terminal_tab()
        self._build_help_tab()

    def _build_flash_tab(self):
        container = ttk.Frame(self.flash_tab, padding=20)
        container.pack(fill=tk.BOTH, expand=True)

        ttk.Label(container, text="Flash Configuration", font=("Segoe UI", 16, "bold")).pack(anchor=tk.W, pady=(0, 20))
        grid = ttk.Frame(container)
        grid.pack(fill=tk.X)

        ttk.Label(grid, text=f"Running on: {platform.system()} ({platform.machine()})", font=("Segoe UI", 9, "italic")).grid(
            row=0, column=0, columnspan=3, sticky=tk.W, pady=(0, 10)
        )

        self.port_var = tk.StringVar(value="/dev/ttyUSB0" if platform.system() == "Linux" else "COM8")
        self.chip_var = tk.StringVar(value="esp32c3")
        self.csv_path_var = tk.StringVar()
        self.mklittlefs_py_var = tk.StringVar(value=self.DEFAULT_MKLITTLEFS_PY)
        self.nvs_gen_py_var = tk.StringVar(value=self.DEFAULT_NVS_GEN_PY)

        ttk.Label(grid, text="Serial Port").grid(row=1, column=0, sticky=tk.W)
        ttk.Entry(grid, textvariable=self.port_var, width=28).grid(row=2, column=0, sticky=tk.W, padx=(0, 20), pady=(5, 10))

        ttk.Label(grid, text="Chip Family").grid(row=1, column=1, sticky=tk.W)
        chips = ["esp32", "esp32s2", "esp32s3", "esp32c2", "esp32c3", "esp32c6", "esp32h2", "esp32p4", "esp8266"]
        ttk.Combobox(grid, textvariable=self.chip_var, values=chips, width=16).grid(row=2, column=1, sticky=tk.W, pady=(5, 10))

        ttk.Label(grid, text="Partitions Definition (.csv)").grid(row=3, column=0, sticky=tk.W, pady=(10, 4))
        ttk.Entry(grid, textvariable=self.csv_path_var).grid(row=4, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid, text="Browse", command=self.browse_csv).grid(row=4, column=2, padx=8)

        ttk.Label(grid, text="mklittlefs.py path").grid(row=5, column=0, sticky=tk.W, pady=(10, 4))
        ttk.Entry(grid, textvariable=self.mklittlefs_py_var).grid(row=6, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid, text="Browse", command=lambda: self._browse_file(self.mklittlefs_py_var, "Python", "*.py")).grid(row=6, column=2, padx=8)

        ttk.Label(grid, text="nvs_partition_gen.py path").grid(row=7, column=0, sticky=tk.W, pady=(10, 4))
        ttk.Entry(grid, textvariable=self.nvs_gen_py_var).grid(row=8, column=0, columnspan=2, sticky=tk.EW)
        ttk.Button(grid, text="Browse", command=lambda: self._browse_file(self.nvs_gen_py_var, "Python", "*.py")).grid(row=8, column=2, padx=8)

        self.gzip_web_var = tk.BooleanVar(value=True)
        self.erase_fs_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(grid, text="Gzip web assets (html, css, js)", variable=self.gzip_web_var).grid(row=9, column=0, columnspan=2, sticky=tk.W, pady=(14, 0))
        ttk.Checkbutton(grid, text="Erase FS partition before flash", variable=self.erase_fs_var).grid(row=10, column=0, columnspan=2, sticky=tk.W, pady=(8, 0))

        grid.columnconfigure(0, weight=1)

        status = ttk.LabelFrame(container, text="Environment Status", padding=10)
        status.pack(fill=tk.X, pady=(18, 0))
        self.status_labels = {}
        for key in ["mklittlefs", "nvsgen", "data", "web", "csv"]:
            lbl = ttk.Label(status, text="", font=("Segoe UI", 10))
            lbl.pack(anchor=tk.W, pady=2)
            self.status_labels[key] = lbl

        btns = ttk.Frame(container)
        btns.pack(fill=tk.X, pady=(20, 0))
        ttk.Button(btns, text="Refresh", command=self.refresh_status).pack(side=tk.LEFT)
        ttk.Button(btns, text="Show Terminal", command=lambda: self.tabs.select(self.terminal_tab)).pack(side=tk.LEFT, padx=(8, 0))
        self.run_btn = ttk.Button(btns, text="Build & Flash LittleFS", command=self.start_flash_thread)
        self.run_btn.pack(side=tk.RIGHT)

    def _build_nvs_tab(self):
        container = ttk.Frame(self.nvs_tab, padding=16)
        container.pack(fill=tk.BOTH, expand=True)

        ttk.Label(container, text="NVS Variables", font=("Segoe UI", 15, "bold")).pack(anchor=tk.W, pady=(0, 10))

        top = ttk.Frame(container)
        top.pack(fill=tk.X)
        ttk.Label(top, text="Namespace").grid(row=0, column=0, sticky=tk.W)
        ttk.Label(top, text="Key").grid(row=0, column=1, sticky=tk.W)
        ttk.Label(top, text="Type").grid(row=0, column=2, sticky=tk.W)
        ttk.Label(top, text="Encoding").grid(row=0, column=3, sticky=tk.W)

        self.nvs_namespace_var = tk.StringVar(value="storage")
        self.nvs_key_var = tk.StringVar()
        self.nvs_type_var = tk.StringVar(value="data")
        self.nvs_encoding_var = tk.StringVar(value="string")
        self.nvs_value_var = tk.StringVar()

        ttk.Entry(top, textvariable=self.nvs_namespace_var, width=20).grid(row=1, column=0, sticky=tk.EW, padx=(0, 8))
        ttk.Entry(top, textvariable=self.nvs_key_var, width=24).grid(row=1, column=1, sticky=tk.EW, padx=(0, 8))
        ttk.Combobox(top, textvariable=self.nvs_type_var, values=["data", "namespace", "file", "key"], width=10).grid(row=1, column=2, sticky=tk.EW, padx=(0, 8))
        ttk.Combobox(top, textvariable=self.nvs_encoding_var,
                     values=["string", "u8", "i8", "u16", "u32", "i32", "base64", "hex2bin", "binary"],
                     width=12).grid(row=1, column=3, sticky=tk.EW, padx=(0, 8))
        ttk.Label(top, text="Value").grid(row=2, column=0, sticky=tk.W, pady=(10, 0))
        ttk.Entry(top, textvariable=self.nvs_value_var).grid(row=3, column=0, columnspan=4, sticky=tk.EW)
        top.columnconfigure(1, weight=1)
        top.columnconfigure(3, weight=1)

        self.nvs_tree = ttk.Treeview(container, columns=("namespace", "key", "type", "encoding", "value"), show="headings", height=13)
        for c, w in [("namespace", 140), ("key", 180), ("type", 90), ("encoding", 110), ("value", 420)]:
            self.nvs_tree.heading(c, text=c.capitalize())
            self.nvs_tree.column(c, width=w, anchor=tk.W)
        self.nvs_tree.pack(fill=tk.BOTH, expand=True, pady=(12, 0))
        self.nvs_tree.bind("<<TreeviewSelect>>", self._on_nvs_select)

        row = ttk.Frame(container)
        row.pack(fill=tk.X, pady=(12, 0))
        ttk.Button(row, text="Add / Update", command=self.add_or_update_nvs).pack(side=tk.LEFT)
        ttk.Button(row, text="Remove Selected", command=self.remove_selected_nvs).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(row, text="Clear", command=self.clear_nvs_form).pack(side=tk.LEFT, padx=(8, 0))

        row2 = ttk.Frame(container)
        row2.pack(fill=tk.X, pady=(8, 0))
        ttk.Button(row2, text="Export CSV", command=self.export_nvs_csv).pack(side=tk.LEFT)
        ttk.Button(row2, text="Import CSV", command=self.import_nvs_csv).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(row2, text="Read from Device", command=self.start_nvs_read_thread).pack(side=tk.RIGHT)
        ttk.Button(row2, text="Write to Device", command=self.start_nvs_write_thread).pack(side=tk.RIGHT, padx=(0, 8))

    def _build_terminal_tab(self):
        frame = ttk.Frame(self.terminal_tab, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)
        self.log_area = tk.Text(frame, bg="#1e1e1e", fg="#ffffff", borderwidth=0, font=("Consolas", 10), wrap=tk.WORD)
        self.log_area.pack(fill=tk.BOTH, expand=True)
        self.log_area.configure(state="disabled")

    def _build_help_tab(self):
        frame = ttk.Frame(self.help_tab, padding=20)
        frame.pack(fill=tk.BOTH, expand=True)
        text = (
            "This uploader now uses Python scripts from lib/.\n\n"
            "Required scripts:\n"
            "- mklittlefs.py\n"
            "- nvs_partition_gen.py\n\n"
            "NVS tab workflow:\n"
            "1) Read from Device to inspect current NVS values\n"
            "2) Add/edit rows in the table\n"
            "3) Write to Device to generate and flash an updated NVS partition"
        )
        ttk.Label(frame, text=text, justify=tk.LEFT, font=("Segoe UI", 10)).pack(anchor=tk.W)

    def log(self, message):
        self.log_area.configure(state="normal")
        self.log_area.insert(tk.END, message + "\n")
        self.log_area.see(tk.END)
        self.log_area.configure(state="disabled")

    def _run_python(self, args, check=True):
        cmd = [sys.executable] + args
        self.log(f">>> {' '.join(cmd)}")
        return subprocess.run(cmd, check=check, capture_output=True, text=True)

    def _run_with_fallbacks(self, variants):
        last = None
        for args in variants:
            try:
                return self._run_python(args, check=True)
            except Exception as exc:
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
        c = {
            "port": self.port_var.get(),
            "chip": self.chip_var.get(),
            "csv_path": self.csv_path_var.get(),
            "gzip_web": self.gzip_web_var.get(),
            "erase_fs": self.erase_fs_var.get(),
            "mklittlefs_py": self.mklittlefs_py_var.get(),
            "nvs_gen_py": self.nvs_gen_py_var.get(),
        }
        with open(self.CONFIG_FILE, "w", encoding="utf-8") as f:
            json.dump(c, f, indent=2)

    def load_config(self):
        if not os.path.isfile(self.CONFIG_FILE):
            return
        try:
            with open(self.CONFIG_FILE, "r", encoding="utf-8") as f:
                c = json.load(f)
            self.port_var.set(c.get("port", self.port_var.get()))
            self.chip_var.set(c.get("chip", self.chip_var.get()))
            self.csv_path_var.set(c.get("csv_path", ""))
            self.gzip_web_var.set(c.get("gzip_web", True))
            self.erase_fs_var.set(c.get("erase_fs", False))
            self.mklittlefs_py_var.set(c.get("mklittlefs_py", self.mklittlefs_py_var.get()))
            self.nvs_gen_py_var.set(c.get("nvs_gen_py", self.nvs_gen_py_var.get()))
        except Exception:
            pass

    def refresh_status(self):
        checks = {
            "mklittlefs": os.path.isfile(self.mklittlefs_py_var.get()),
            "nvsgen": os.path.isfile(self.nvs_gen_py_var.get()),
            "data": os.path.isdir(self.DATA_DIR) and len(os.listdir(self.DATA_DIR)) > 0,
            "web": os.path.isdir(self.WEB_DIR),
            "csv": bool(self.csv_path_var.get() and os.path.isfile(self.csv_path_var.get())),
        }
        self.status_labels["mklittlefs"].config(text=f"mklittlefs.py: {'Found' if checks['mklittlefs'] else 'Missing'}")
        self.status_labels["nvsgen"].config(text=f"nvs_partition_gen.py: {'Found' if checks['nvsgen'] else 'Missing'}")
        self.status_labels["data"].config(text=f"data/: {'OK' if checks['data'] else 'Missing or empty'}")
        self.status_labels["web"].config(text=f"web/: {'Found' if checks['web'] else 'Missing'}")
        self.status_labels["csv"].config(text=f"partitions.csv: {'Found' if checks['csv'] else 'Not selected'}")
        ready = all(checks.values())
        self.run_btn.state(["!disabled"] if ready else ["disabled"])
        return ready

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
        threading.Thread(target=self.run_flash, daemon=True).start()

    def run_flash(self):
        self.tabs.select(self.terminal_tab)
        if not self.refresh_status():
            self.log("[ERROR] Environment checks are not ready.")
            return
        self.save_config()

        try:
            fs = self.get_partition("spiffs")
            self.log(f">>> LittleFS partition offset={hex(fs['offset'])}, size={hex(fs['size'])}")

            staging = tempfile.mkdtemp(prefix="littlefs_staging_")
            try:
                for f in os.listdir(self.DATA_DIR):
                    src = os.path.join(self.DATA_DIR, f)
                    if os.path.isfile(src):
                        shutil.copy2(src, staging)

                web_staging = os.path.join(staging, "web")
                os.makedirs(web_staging, exist_ok=True)
                for f in self.WEB_FILES:
                    src = os.path.join(self.WEB_DIR, f)
                    if not os.path.isfile(src):
                        continue
                    if self.gzip_web_var.get() and os.path.splitext(f)[1] in {".html", ".css", ".js", ".ico"}:
                        dst = os.path.join(web_staging, f + ".gz")
                        with open(src, "rb") as fin, gzip.open(dst, "wb") as fout:
                            shutil.copyfileobj(fin, fout)
                    else:
                        shutil.copy2(src, web_staging)

                self._run_with_fallbacks([
                    [self.mklittlefs_py_var.get(), "-c", staging, "-b", "4096", "-p", "256", "-s", str(fs["size"]), self.IMAGE_NAME],
                    [self.mklittlefs_py_var.get(), "create", "--path", staging, "--block-size", "4096", "--page-size", "256", "--size", str(fs["size"]), "--output", self.IMAGE_NAME],
                ])
            finally:
                shutil.rmtree(staging, ignore_errors=True)

            if self.erase_fs_var.get():
                self._run_python([
                    "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", "921600",
                    "erase-region", hex(fs["offset"]), hex(fs["size"]),
                ])

            res = self._run_python([
                "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", "921600",
                "write-flash", hex(fs["offset"]), self.IMAGE_NAME,
            ])
            if res.stdout:
                self.log(res.stdout)
            self.log("[SUCCESS] LittleFS flashed successfully.")
            messagebox.showinfo("Success", "Filesystem flashed successfully.")
        except Exception as e:
            self.log(f"[ERROR] {e}")
            messagebox.showerror("Error", str(e))
        finally:
            if os.path.isfile(self.IMAGE_NAME):
                os.remove(self.IMAGE_NAME)

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

    def remove_selected_nvs(self):
        for i in self.nvs_tree.selection():
            self.nvs_tree.delete(i)

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
        self._load_nvs_csv_to_tree(p)
        self.log(f">>> Imported NVS CSV: {p}")

    def _load_nvs_csv_to_tree(self, csv_path):
        self.nvs_tree.delete(*self.nvs_tree.get_children())
        current_ns = "storage"
        with open(csv_path, "r", encoding="utf-8") as f:
            reader = csv.reader(f)
            for row in reader:
                if not row or row[0] == "key" or row[0].startswith("#"):
                    continue
                key = row[0].strip() if len(row) > 0 else ""
                typ = row[1].strip() if len(row) > 1 else "data"
                enc = row[2].strip() if len(row) > 2 else "string"
                val = row[3].strip() if len(row) > 3 else ""
                if typ == "namespace":
                    current_ns = key
                    continue
                self.nvs_tree.insert("", tk.END, values=(current_ns, key, typ, enc, val))

    def start_nvs_read_thread(self):
        threading.Thread(target=self.read_nvs_from_device, daemon=True).start()

    def read_nvs_from_device(self):
        self.tabs.select(self.terminal_tab)
        try:
            nvs = self.get_partition("nvs")
            self.log(f">>> NVS partition offset={hex(nvs['offset'])}, size={hex(nvs['size'])}")
            with tempfile.TemporaryDirectory(prefix="nvs_read_") as tmp:
                bin_path = os.path.join(tmp, "nvs.bin")
                csv_path = os.path.join(tmp, "nvs.csv")
                self._run_python([
                    "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", "921600",
                    "read-flash", hex(nvs["offset"]), hex(nvs["size"]), bin_path,
                ])
                self._run_with_fallbacks([
                    [self.nvs_gen_py_var.get(), "parse", bin_path, csv_path, str(nvs["size"])],
                    [self.nvs_gen_py_var.get(), "parse", "--input", bin_path, "--output", csv_path, "--size", str(nvs["size"])],
                ])
                self._load_nvs_csv_to_tree(csv_path)
            self.tabs.select(self.nvs_tab)
            self.log("[SUCCESS] NVS values loaded from device.")
        except Exception as e:
            self.log(f"[ERROR] {e}")
            messagebox.showerror("NVS read failed", str(e))

    def start_nvs_write_thread(self):
        threading.Thread(target=self.write_nvs_to_device, daemon=True).start()

    def write_nvs_to_device(self):
        self.tabs.select(self.terminal_tab)
        try:
            nvs = self.get_partition("nvs")
            if not self._tree_rows():
                raise ValueError("NVS table is empty.")

            with tempfile.TemporaryDirectory(prefix="nvs_write_") as tmp:
                csv_path = os.path.join(tmp, "nvs.csv")
                bin_path = os.path.join(tmp, "nvs.bin")
                self._write_nvs_csv(csv_path)

                self._run_with_fallbacks([
                    [self.nvs_gen_py_var.get(), "generate", csv_path, bin_path, str(nvs["size"])],
                    [self.nvs_gen_py_var.get(), "generate", "--input", csv_path, "--output", bin_path, "--size", str(nvs["size"])],
                ])
                self._run_python([
                    "-m", "esptool", "--chip", self.chip_var.get(), "--port", self.port_var.get(), "--baud", "921600",
                    "write-flash", hex(nvs["offset"]), bin_path,
                ])

            self.log("[SUCCESS] NVS partition flashed successfully.")
            messagebox.showinfo("Success", "NVS variables written successfully.")
        except Exception as e:
            self.log(f"[ERROR] {e}")
            messagebox.showerror("NVS write failed", str(e))


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
            "Missing dependencies",
            "Install missing packages:\n\n    pip install " + " ".join(missing),
        )
        root.destroy()
        sys.exit(1)


if __name__ == "__main__":
    check_dependencies()
    root = tk.Tk()
    ESPUploaderGUI(root)
    root.mainloop()
