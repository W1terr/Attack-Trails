r"""Builds the mod archive "Attack Trails <version>.zip": SKSE\Plugins\AttackTrails.dll,
meshes\AttackTrails\Particles.nif + Line.nif and textures\AttackTrails\Particle.dds + Line.dds (tools/make_assets.py).
The version is the target's set_version in xmake.lua (a trailing ".0" is left out of the name).
usage: python package.py [build dir] [output dir]"""

import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build"
OUTPUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "dist"


def version(target: str) -> str:
    text = (ROOT / "xmake.lua").read_text(encoding="utf-8")
    match = re.search(r'target\("' + re.escape(target) + r'"\)\s*\n\s*set_version\("([^"]+)"\)', text)
    full = match.group(1) if match else "0.0.0"
    return re.sub(r"\.0$", "", full)


def main():
    plugin = BUILD / "windows" / "x64" / "releasedbg"
    res = ROOT / "res"
    files = {
        "SKSE/Plugins/AttackTrails.dll": plugin / "AttackTrails.dll",
        "meshes/AttackTrails/Particles.nif": res / "meshes" / "AttackTrails" / "Particles.nif",
        "meshes/AttackTrails/Line.nif": res / "meshes" / "AttackTrails" / "Line.nif",
        "textures/AttackTrails/Line.dds": res / "textures" / "AttackTrails" / "Line.dds",
        "textures/AttackTrails/Particle.dds": res / "textures" / "AttackTrails" / "Particle.dds",
    }
    lang = res / "SKSE" / "Plugins" / "AttackTrails" / "lang"
    for path in sorted(lang.glob("*.json")) if lang.exists() else []:
        files[f"SKSE/Plugins/AttackTrails/lang/{path.name}"] = path
    missing = [str(p) for p in files.values() if not p.exists()]
    if missing:
        sys.exit("missing: " + ", ".join(missing) + " (assets: run tools/make_assets.py)")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    archive = OUTPUT / f"Attack Trails {version('AttackTrails')}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
        for entry, path in files.items():
            zf.write(path, entry)
    print(f"wrote {archive} ({archive.stat().st_size / 1024:.0f} KB, {len(files)} files)")


if __name__ == "__main__":
    main()
