#!/usr/bin/env python3
"""Scaffold a Glaze game package from the glaze-new-app templates.

usage (from the glaze-apps root):
  python3 .claude/skills/glaze-new-app/scripts/new_app.py <id> "<Name>" [RRGGBB]

Creates packages/<id>/ (Info, Makefile, game.h, game.c, main.c, README.md,
icon.gzi), tests/native/<id>.c, a <id>-test target in tests/Makefile (also
added to `check:`), and adds <id> to the Games row of README.md. Refuses to
overwrite anything.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEMPLATES = HERE.parent / "templates"


def fail(msg):
    print("new_app: " + msg, file=sys.stderr)
    sys.exit(1)


def fill(text, app_id, name):
    return (text.replace("__ID__", app_id).replace("__NAME__", name)
            .replace("__GUARD__", app_id.upper()))


def main():
    if len(sys.argv) not in (3, 4):
        fail(__doc__.strip())
    app_id, name = sys.argv[1], sys.argv[2]
    color = sys.argv[3] if len(sys.argv) == 4 else "3C8F46"
    root = Path.cwd()
    if not (root / "packages").is_dir() or not (root / "tests/Makefile").is_file():
        fail("run from the glaze-apps repository root")
    # Stricter than the SDK (which also allows . and -): the id is reused in
    # make targets and C include guards.
    if not re.fullmatch(r"[a-z][a-z0-9]{0,23}", app_id):
        fail("id must be 1-24 chars: a lowercase letter, then lowercase letters/digits")
    if not (1 <= len(name) <= 31 and name.isascii() and name.isprintable()):
        fail("name must be 1-31 printable ASCII characters")
    if not re.fullmatch(r"[0-9A-Fa-f]{6}", color):
        fail("color must be RRGGBB hex")
    pkg = root / "packages" / app_id
    test = root / "tests/native" / (app_id + ".c")
    if pkg.exists() or test.exists():
        fail(f"{pkg} or {test} already exists")
    makefile = root / "tests/Makefile"
    mk = makefile.read_text()
    if re.search(rf"^{app_id}-test:", mk, re.M):
        fail(f"tests/Makefile already has {app_id}-test")

    pkg.mkdir()
    for src in ("Info", "Makefile", "game.h", "game.c", "main.c", "README.md"):
        (pkg / src).write_text(fill((TEMPLATES / src).read_text(), app_id, name))
    test.write_text(fill((TEMPLATES / "test.c").read_text(), app_id, name))
    subprocess.run([sys.executable, str(HERE / "mkicon.py"), str(pkg / "icon.gzi"), color], check=True)

    mk, n = re.subn(r"^(check:.*)$", rf"\1 {app_id}-test", mk, count=1, flags=re.M)
    if n != 1:
        fail("no check: line in tests/Makefile")
    mk = mk.rstrip("\n") + "\n" + fill((TEMPLATES / "make-target.mk").read_text(), app_id, name)
    makefile.write_text(mk)

    readme = root / "README.md"
    text = readme.read_text()
    text, n = re.subn(r"^(\| .*) \| Games \|$", rf"\1, `{app_id}` | Games |", text, count=1, flags=re.M)
    if n == 1:
        readme.write_text(text)
    else:
        print("new_app: add the app to the README.md table by hand", file=sys.stderr)

    print(f"created packages/{app_id}/ and tests/native/{app_id}.c; registered {app_id}-test")
    print(f"next: make -C tests {app_id}-test && .claude/skills/glaze-build/scripts/build_app.sh {app_id}")


if __name__ == "__main__":
    main()
