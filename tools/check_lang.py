"""Checks the translations in res/SKSE/Plugins/AttackTrails/lang against the texts the menu shows.

The English texts are the keys: every string literal in Menu.cpp except ImGui ids, number formats and log lines, and
the names in Settings.h (kElementNames, kWeaponFilterNames).
Reports keys missing from a language file, keys the code no longer uses and broken {} placeholders.
usage: python check_lang.py [--keys]   (--keys: print the keys, one per line)
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANG_DIR = ROOT / "res" / "SKSE" / "Plugins" / "AttackTrails" / "lang"
LANGUAGES = ["french", "german", "italian", "spanish", "polish", "russian", "japanese", "chinese", "simplified_chinese"]
LIT = r'"((?:[^"\\]|\\.)*)"'

# literals in Menu.cpp that are not shown text
NOT_TEXT = {"Attack Trails", "x", "", "auto", "(?)", "colors", "weapons", "enchantments", "RenameSection",
            "SKSEMenuFramework.h", "Menu.h", "Lang.h", "Settings.h", "Trails.h"}


def literals(text):
    text = re.sub(r"//[^\n]*", "", text)
    return [m.encode().decode("unicode_escape").encode("latin-1").decode("utf-8") for m in re.findall(LIT, text)]


def code_keys():
    found = []
    for lit in literals((ROOT / "src" / "Menu.cpp").read_text(encoding="utf-8")):
        if lit in NOT_TEXT or lit.startswith(("#", "%")) or "###" in lit or lit in ("{}", "{}/{}", "{} {:06X}"):
            continue
        if lit.startswith("SKSE Menu Framework is not") or lit.startswith("SKSE Menu Framework {}"):
            continue  # log lines
        found.append(lit)
    settings = (ROOT / "src" / "Settings.h").read_text(encoding="utf-8")
    for name in ("kElementNames", "kWeaponFilterNames"):
        found += literals(re.search(name + r"\{([^}]*)\}", settings).group(1))
    keys = []
    for key in found:
        if key not in keys:
            keys.append(key)
    return keys


def main():
    keys = code_keys()
    if "--keys" in sys.argv:
        print("\n".join(keys))
        return
    ok = True
    for lang in LANGUAGES:
        path = LANG_DIR / f"{lang}.json"
        if not path.exists():
            print(f"{lang}: missing file")
            ok = False
            continue
        table = json.loads(path.read_text(encoding="utf-8"))
        missing = [k for k in keys if not table.get(k)]
        unused = [k for k in table if k not in keys]
        broken = [k for k, v in table.items() if v and sorted(re.findall(r"\{[^}]*\}", k)) != sorted(re.findall(r"\{[^}]*\}", v))]
        for what, items in (("missing", missing), ("unused", unused), ("broken placeholders", broken)):
            for item in items:
                print(f"{lang}: {what}: {item}")
                ok = False
    print(f"{len(keys)} keys, {'all languages complete' if ok else 'problems above'}")


if __name__ == "__main__":
    main()
