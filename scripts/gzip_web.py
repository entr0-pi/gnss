Import("env")

import os
import io
import sys
import sysconfig
import importlib.util
import argparse
import textwrap

_SCRIPT_PATH = os.path.abspath(globals().get("__file__", sys.argv[0] if sys.argv else ""))

def _load_stdlib_gzip():
	# Avoid shadowing stdlib gzip when this script is named gzip.py
	try:
		import gzip as _gzip
	except Exception:
		_gzip = None
	if _gzip is None or os.path.abspath(getattr(_gzip, "__file__", "")) == _SCRIPT_PATH:
		stdlib_dir = sysconfig.get_paths().get("stdlib")
		if stdlib_dir:
			gzip_path = os.path.join(stdlib_dir, "gzip.py")
			if os.path.isfile(gzip_path):
				spec = importlib.util.spec_from_file_location("stdlib_gzip", gzip_path)
				if spec and spec.loader:
					mod = importlib.util.module_from_spec(spec)
					spec.loader.exec_module(mod)
					return mod
	return _gzip

gzip = _load_stdlib_gzip()

ROOT = env.get("PROJECT_DIR") if "env" in globals() else os.path.dirname(_SCRIPT_PATH)

PAIRS = {
	"web/app.js": "include/app_js.h",
	"web/favicon.ico": "include/app_favicon.h",
	"web/index.html": "include/app_index.h",
	"web/style.css": "include/app_style.h",
}

def _webui_enabled_from_env():
	# If PlatformIO env is not available, default to enabled (direct script use).
	if "env" not in globals():
		return True
	defs = env.get("CPPDEFINES") or []
	for d in defs:
		if isinstance(d, (list, tuple)) and len(d) >= 1:
			name = str(d[0])
			val = d[1] if len(d) > 1 else None
		else:
			name = str(d)
			val = None
		if name == "WEBUI_ENABLE":
			if val is None:
				return True
			try:
				return int(str(val), 0) != 0
			except ValueError:
				return str(val).lower() not in ("0", "false", "off", "no")
	return True

def to_varname(header_path):
	name = os.path.splitext(os.path.basename(header_path))[0]
	return "".join(c if c.isalnum() else "_" for c in name).upper()

def gzip_bytes(data):
	if gzip is None:
		raise RuntimeError("stdlib gzip module could not be loaded; rename gzip.py")
	# Prefer gzip.compress (present in most environments)
	if hasattr(gzip, "compress"):
		gz = gzip.compress(data)
		# normalize MTIME (bytes 4..7) for deterministic output
		if len(gz) >= 10:
			b = bytearray(gz)
			b[4:8] = b'\x00\x00\x00\x00'
			return bytes(b)
		return gz

	# Fallback: use GzipFile if available
	GzipFile = getattr(gzip, "GzipFile", None)
	if GzipFile is None:
		raise RuntimeError("no gzip.compress or gzip.GzipFile available")
	buf = io.BytesIO()
	try:
		with GzipFile(fileobj=buf, mode="wb", mtime=0) as gf:
			gf.write(data)
		return buf.getvalue()
	except TypeError:
		# older GzipFile may not accept mtime kwarg
		buf = io.BytesIO()
		with GzipFile(fileobj=buf, mode="wb") as gf:
			gf.write(data)
		gz = buf.getvalue()
		if len(gz) >= 10:
			b = bytearray(gz)
			b[4:8] = b'\x00\x00\x00\x00'
			return bytes(b)
		return gz

def make_c_array(data, bytes_per_line=12):
	parts = []
	for i in range(0, len(data), bytes_per_line):
		seg = data[i:i+bytes_per_line]
		line = ", ".join(f"0x{b:02x}" for b in seg)
		parts.append("    " + line)
	return ",\n".join(parts)

def write_text_header(header_path, varname, content, tag):
	# tag should be simple alnum identifier like HTML, CSS, JAVASCRIPT
	# use raw-string literal to preserve content
	header = textwrap.dedent(f"""\
		#pragma once
		#include <Arduino.h>

		// text resource
		const char {varname}[] PROGMEM = R\"{tag}(
{content}
){tag}\";

		const unsigned int {varname}_LEN = sizeof({varname}) - 1;
	""")
	os.makedirs(os.path.dirname(header_path), exist_ok=True)
	with open(header_path, "w", newline="\n", encoding="utf-8") as f:
		f.write(header)

def write_binary_header(header_path, varname, gz_bytes_val):
	header = textwrap.dedent(f"""\
		#pragma once
		#include <Arduino.h>

		// gzip-compressed data
		const unsigned char {varname}[] PROGMEM = {{
		{make_c_array(gz_bytes_val)}
		}};

		const unsigned int {varname}_LEN = sizeof({varname});
	""")
	os.makedirs(os.path.dirname(header_path), exist_ok=True)
	with open(header_path, "w", newline="\n", encoding="utf-8") as f:
		f.write(header)

def process_pair(src_rel, hdr_rel):
	src = os.path.join(ROOT, src_rel)
	hdr = os.path.join(ROOT, hdr_rel)
	if not os.path.isfile(src):
		print(f"Skipping missing source: {src_rel}")
		return
	with open(src, "rb") as f:
		data = f.read()

	ext = os.path.splitext(src_rel)[1].lower()
	varname = to_varname(hdr)

	# Gzip all files and emit byte array
	gz = gzip_bytes(data)
	# validate roundtrip
	try:
		dec = gzip.decompress(gz)
	except Exception as e:
		print(f"ERROR: gzip decompression failed for {src_rel}: {e}")
		return
	if dec != data:
		print(f"ERROR: roundtrip mismatch for {src_rel}")
		return
	write_binary_header(hdr, varname, gz)
	print(f"Wrote binary header {hdr_rel} ({len(gz)} bytes gzipped) -> {varname}")

def main(argv=None):
	if not _webui_enabled_from_env():
		print("WEBUI_ENABLE=0: skipping gzip_web.py")
		return
	parser = argparse.ArgumentParser(description="Generate PROGMEM headers from web/* files.")
	parser.add_argument("files", nargs="*", help="optional subset of source paths (relative to repo root) to process")
	args, _ = parser.parse_known_args(argv)
	if args.files:
		items = {k:v for k,v in PAIRS.items() if k in args.files or v in args.files}
	else:
		items = PAIRS
	for src, hdr in items.items():
		process_pair(src, hdr)

if __name__ == "__main__":
	main()
elif "env" in globals():
	# PlatformIO imports extra_scripts; execute on import without parsing SCons args.
	main([])
