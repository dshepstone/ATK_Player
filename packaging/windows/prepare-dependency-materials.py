"""Prepare pinned license notices and corresponding source for Windows packaging.

No application code is compiled or changed. Generated files stay in build output.
Only checksummed upstream archives are used; dependency/version drift fails closed.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
EXCLUDED_SOURCE_SUFFIXES = {
    ".exe", ".dll", ".msi", ".cab", ".lib", ".obj", ".pdb", ".wixlib", ".a", ".so", ".dylib",
    ".ttf", ".otf", ".ttc", ".woff", ".woff2",
    ".pfx", ".p12", ".pem", ".key", ".snk", ".cer", ".crt",
}


def source_only(files: dict[str, bytes], exclusions: dict, name: str) -> dict[str, bytes]:
    omitted = [path for path, data in files.items() if PurePosixPath(path).suffix.lower() in EXCLUDED_SOURCE_SUFFIXES
               # Qt's Info.plist.lib files are XML build templates, not libraries.
               and not (PurePosixPath(path).suffix.lower() == ".lib" and data.lstrip().startswith(b"<?xml"))]
    exclusions[name] = sorted(omitted)
    omitted_set = set(omitted)
    return {path: data for path, data in files.items() if path not in omitted_set}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(record: dict, cache: Path) -> Path:
    name = record["name"]
    if Path(name).name != name:
        raise ValueError("Source archive name must be a basename")
    path = cache / name
    if not path.exists():
        partial = path.with_suffix(path.suffix + ".partial")
        print(f"Downloading {name}", flush=True)
        with urllib.request.urlopen(record["url"], timeout=120) as source, partial.open("wb") as target:
            shutil.copyfileobj(source, target)
        if sha256(partial) != record["sha256"]:
            raise ValueError(f"Source archive checksum mismatch: {name}")
        partial.replace(path)
    if sha256(path) != record["sha256"]:
        raise ValueError(f"Cached source archive checksum mismatch: {name}")
    return path


def write_text(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def license_headers(files: dict[str, bytes]) -> str:
    """Retain upstream copyright/license comment blocks, with their file paths.

    Include the entire source-wide corpus, including unselected files, rather
    than guessing which object survived upstream linking. Full source is bundled.
    """
    result = []
    for name, data in sorted(files.items()):
        if PurePosixPath(name).suffix.lower() not in {".c", ".cpp", ".h", ".hpp", ".s", ".asm", ".rc"}:
            continue
        text = data[:16384].decode("utf-8", errors="replace")
        blocks = re.findall(r"/\*.*?\*/", text, re.S)
        blocks += re.findall(r"(?m)(?:(?:\s*//|\s*;|\s*#)[^\n]*\n)+", text)
        selected = [block for block in blocks if re.search(r"copyright|SPDX-License|permission is hereby|public domain", block, re.I)]
        if selected:
            result.append(f"\n--- {name} ---\n" + "\n".join(selected))
    return "\n".join(result)


def qt_materials(record: dict, archive: Path, qt_root: Path, output: Path, exclusions: dict) -> None:
    module = record["name"].split("-6.")[0]
    with tarfile.open(archive) as source:
        members = {m.name: m for m in source if m.isfile()}
        root = next(iter(members)).split("/")[0]

        def read(name: str) -> bytes:
            stream = source.extractfile(members[name])
            assert stream is not None
            return stream.read()

        if read(root + "/.tag").decode().strip() != record["revision"]:
            raise ValueError(f"Unexpected source revision for {module}")
        sbom_path = qt_root / "sbom" / f"{module}-6.9.3.spdx.json"
        sbom = json.loads(sbom_path.read_text(encoding="utf-8"))
        package = next(p for p in sbom["packages"] if p["name"] == module)
        recorded_location = package.get("downloadLocation", "")
        if "@" in recorded_location and recorded_location.split("@")[-1] != record["revision"]:
            raise ValueError(f"Qt source revision differs from installed SBOM for {module}")
        notes = [f"Qt {module} 6.9.3 — unmodified upstream source notices\n",
                 "Copyright (C) The Qt Company Ltd. and other contributors.\n",
                 "The deployed Qt libraries use the LGPL-3.0 option. Qt entry-point code uses BSD-3-Clause.\n",
                 "This software is based in part on the work of the Independent JPEG Group.\n",
                 "Portions of this software are copyright (C) The FreeType Project (www.freetype.org). All rights reserved.\n",
                 "This complete upstream notice corpus also describes build/test/unselected code.\n"
                 "Its inclusion does not enable GPL-only modules, codecs or the Qt FFmpeg backend.\n"]
        for name in sorted(members):
            if name.startswith(root + "/LICENSES/"):
                notes.append(f"\n--- {name} ---\n" + read(name).decode("utf-8", errors="replace"))
            if not name.endswith("qt_attribution.json"):
                continue
            # Qt attribution inputs sometimes contain literal newlines in strings.
            attribution = json.loads(read(name), strict=False)
            for entry in attribution if isinstance(attribution, list) else [attribution]:
                notes.append(f"\n--- {name} ---\n" + json.dumps(entry, indent=2, ensure_ascii=False))
                for field in ("LicenseFile", "LicenseFiles", "CopyrightFile"):
                    values = entry.get(field, [])
                    for value in values if isinstance(values, list) else [values]:
                        linked = posixpath.normpath(posixpath.join(posixpath.dirname(name), value))
                        if linked not in members:
                            raise ValueError(f"Missing Qt notice source: {linked}")
                        notes.append(f"\n--- {linked} ---\n" + read(linked).decode("utf-8", errors="replace"))
        # Preserve per-file notices, including BSD entry-point copyright/terms.
        code = {n: read(n) for n in members if PurePosixPath(n).suffix.lower() in {".cpp", ".h", ".c"}}
        notes.append(license_headers(code))
        write_text(output / f"Qt-{module}-NOTICES.txt", "\n".join(notes))
        if module == "qtbase":
            write_text(output / "GPL-3.0.txt", read(root + "/LICENSES/GPL-3.0-only.txt").decode())
        shutil.copyfile(sbom_path, output / f"Qt-{module}-6.9.3.spdx.json")
        for suffix in ("opt", "summary"):
            config = qt_root / f"config_{module}.{suffix}"
            if not config.is_file():
                raise ValueError(f"Missing Qt build configuration: {config}")
            shutil.copyfile(config, output / config.name)
        # Keep source/build inputs, not unused binary/font fixtures. Record all
        # omissions; in particular these fonts are not Windows runtime fonts.
        if module == "qtbase":
            name = "qtbase-6.9.3-source.zip"
            files = {n[len(root) + 1:]: read(n) for n in members}
            zip_files(output / "sources" / name, source_only(files, exclusions, name))
        else:
            shutil.copyfile(archive, output / "sources" / record["name"])


def zip_files(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)


def ffmpeg_materials(archive: Path, vcpkg_archive: Path, runtime: Path, output: Path) -> None:
    with zipfile.ZipFile(vcpkg_archive) as vcpkg:
        root = vcpkg.namelist()[0].split("/")[0]
        port_prefix = root + "/ports/ffmpeg/"
        port = vcpkg.read(port_prefix + "portfile.cmake").decode()
        lock = json.loads((HERE / "dependency-sources.json").read_text())
        record = next(r for r in lock["archives"] if r["name"].startswith("ffmpeg-"))
        if record["sha512"] not in port:
            raise ValueError("FFmpeg source digest differs from the pinned vcpkg port")
        patch_block = port.split("    PATCHES\n", 1)[1].split("\n)", 1)[0]
        patches = re.findall(r"(?m)^\s+(\S+\.patch)\s*(?:#.*)?$", patch_block)
        if not patches:
            raise ValueError("No pinned FFmpeg patches found")
        port_files = {n[len(root) + 1:]: vcpkg.read(n) for n in vcpkg.namelist() if n.startswith(port_prefix) and not n.endswith("/")}
    with tempfile.TemporaryDirectory(prefix="ffmpeg-source-", dir=output.parent) as temporary:
        temp = Path(temporary)
        with tarfile.open(archive) as source:
            for member in source.getmembers():
                parts = PurePosixPath(member.name).parts
                if not parts or member.name.startswith("/") or ".." in parts or not (member.isfile() or member.isdir()):
                    raise ValueError("Unsafe member in FFmpeg source archive")
            source.extractall(temp, filter="data")
        source_root = next(p for p in temp.iterdir() if p.is_dir())
        subprocess.run(["git", "-C", str(source_root), "init", "-q"], check=True)
        for patch in patches:
            patch_path = temp / patch
            patch_path.write_bytes(port_files["ports/ffmpeg/" + patch])
            subprocess.run(["git", "-C", str(source_root), "-c", "core.autocrlf=false",
                            "apply", "--ignore-whitespace", "--whitespace=nowarn", str(patch_path)], check=True)
        files = {p.relative_to(source_root).as_posix(): p.read_bytes() for p in source_root.rglob("*")
                 if p.is_file() and ".git" not in p.relative_to(source_root).parts}
        zip_files(output / "sources" / "FFmpeg-9.0.1-patched-source.zip", files)
        notes = ["FFmpeg 9.0.1, vcpkg port revision 1 — LGPL-2.1-or-later runtime\n",
                 "Copyright (c) 2000-2026 the FFmpeg developers.\n",
                 "Complete upstream source-wide notice corpus, including unselected files.\n"
                 "The runtime configuration disables GPL/nonfree; the presence of GPL source notices does not enable that code.\n"]
        for name, data in sorted(files.items()):
            if name.startswith("COPYING") or name == "LICENSE.md":
                notes.append(f"\n--- {name} ---\n" + data.decode("utf-8", errors="replace"))
        notes.append(license_headers(files))
        write_text(output / "FFmpeg-NOTICES.txt", "\n".join(notes))
        zip_files(output / "sources" / "FFmpeg-vcpkg-port.zip", port_files)
    configurations = {}
    with os.add_dll_directory(str(runtime)):
        for prefix in ("avcodec", "avformat", "avutil", "swresample", "swscale"):
            paths = list(runtime.glob(prefix + "-*.dll"))
            if len(paths) != 1:
                raise ValueError(f"Expected exactly one {prefix} runtime")
            library = ctypes.CDLL(str(paths[0]))
            config = getattr(library, prefix + "_configuration")
            license_fn = getattr(library, prefix + "_license")
            config.restype = license_fn.restype = ctypes.c_char_p
            text = config().decode()
            license_text = license_fn().decode()
            if "LGPL version 2.1 or later" not in license_text or any(flag in text for flag in ("--enable-gpl", "--enable-nonfree", "--enable-version3")):
                raise ValueError("Unexpected FFmpeg runtime license/configuration")
            if not all(flag in text for flag in ("--disable-libx264", "--disable-libx265", "--disable-libfdk-aac")):
                raise ValueError("Prohibited codec configuration")
            configurations[paths[0].name] = dict(sha256=sha256(paths[0]), configuration=text, license=license_text)
    write_text(output / "FFmpeg-BUILD-CONFIGURATION.json", json.dumps(configurations, indent=2) + "\n")


def prepare(args: argparse.Namespace) -> None:
    lock = json.loads((HERE / "dependency-sources.json").read_text())
    manifest = json.loads((REPO / "vcpkg.json").read_text())
    if manifest["builtin-baseline"] != lock["vcpkg_baseline"] or args.wix_version != lock["wix_version"]:
        raise ValueError("Dependency versions differ from the source lock; update and audit the lock")
    output, cache = args.output.resolve(), args.cache.resolve()
    # Temporary source trees are deleted by TemporaryDirectory: constrain both
    # their parent and downloaded/generated output to the intended build tree.
    output.relative_to((REPO / "build").resolve())
    cache.relative_to((REPO / "build").resolve())
    output.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    (output / "sources").mkdir(exist_ok=True)
    archives = {r["name"]: fetch(r, cache) for r in lock["archives"]}
    exclusions = {}
    for record in lock["archives"]:
        if record["name"].startswith("qt"):
            print(f"Preparing {record['name']} notices/source", flush=True)
            qt_materials(record, archives[record["name"]], args.qt_root.resolve(), output, exclusions)
    print("Applying pinned FFmpeg source patches", flush=True)
    ffmpeg_materials(archives["ffmpeg-9.0.1.tar.gz"], archives["vcpkg-source.zip"], args.ffmpeg_runtime.resolve(), output)
    with zipfile.ZipFile(archives["wix-4.0.6.zip"]) as wix:
        license_text = wix.read("wix-4.0.6/LICENSE.TXT").decode()
        write_text(output / "WiX-MS-RL.txt", license_text)
        files = {n: wix.read(n) for n in wix.namelist() if not n.endswith("/") and
                 ("/src/ext/Util/ca/" in n or "/src/ext/UI/ca/" in n or "/src/libs/" in n)}
        write_text(output / "WiX-NOTICES.txt", "Copyright (c) .NET Foundation and contributors.\n"
                   "WiX Toolset 4.0.6 installer custom actions and UI resources use MS-RL.\n\n" + license_text + license_headers(files))
    for upstream_name, name in (("wix-4.0.6.zip", "WiX-4.0.6-source.zip"), ("vcpkg-source.zip", "vcpkg-build-source.zip")):
        with zipfile.ZipFile(archives[upstream_name]) as upstream:
            files = {n: upstream.read(n) for n in upstream.namelist() if not n.endswith("/")}
        zip_files(output / "sources" / name, source_only(files, exclusions, name))
    write_text(output / "SOURCE-EXCLUSIONS.json", json.dumps(exclusions, indent=2) + "\n")
    # Retire only these known superseded generated files, within checked output.
    for old_name in ("qtbase-6.9.3.tar.xz", "wix-4.0.6.zip", "vcpkg-source.zip"):
        old = output / "sources" / old_name
        if old.is_file():
            old.unlink()
    tracked = subprocess.check_output(["git", "-C", str(REPO), "ls-files", "-z"]).decode().split("\0")
    tracked += ["packaging/windows/dependency-sources.json", "packaging/windows/prepare-dependency-materials.py",
                "packaging/windows/SOURCE_MATERIALS.md", "packaging/windows/tests/test_dependency_materials.py",
                "docs/SIGNPATH_READINESS_AUDIT.md"]
    # Include the actual current MIT source needed to rebuild/relink this app.
    app_files = {name: (REPO / name).read_bytes() for name in set(tracked) if name and (REPO / name).is_file()}
    zip_files(output / "sources" / "ATK-Player-source.zip", app_files)
    shutil.copyfile(HERE / "dependency-sources.json", output / "DEPENDENCY-SOURCE-LOCK.json")
    shutil.copyfile(HERE / "SOURCE_MATERIALS.md", output / "SOURCE_MATERIALS.md")
    hashes = {p.relative_to(output).as_posix(): sha256(p) for p in sorted(output.rglob("*")) if p.is_file() and p.name != "SHA256SUMS.txt"}
    write_text(output / "SHA256SUMS.txt", "".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
    print(f"Prepared {len(hashes)} dependency materials in {output}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-root", type=Path, required=True)
    parser.add_argument("--ffmpeg-runtime", type=Path, required=True)
    parser.add_argument("--wix-version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    prepare(parser.parse_args())
