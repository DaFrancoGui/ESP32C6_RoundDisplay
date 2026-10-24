"""Export only reusable project sources; never overwrite an existing directory."""
import argparse
from pathlib import Path
import shutil


def source_files(root):
    for name in ("CMakeLists.txt", "sdkconfig.defaults", "dependencies.lock", "README.md", ".gitignore"):
        path = root / name
        if path.is_file():
            yield path
    for name in ("components", "main", "tests", "tools"):
        for path in sorted((root / name).rglob("*")):
            if path.is_file() and "__pycache__" not in path.parts and path.suffix != ".pyc":
                yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--dry-run", action="store_true", help="List files without creating the destination")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    destination = args.destination.resolve()
    if destination == root or root in destination.parents:
        parser.error("Destination must be outside the source project")
    if destination.exists():
        parser.error("Destination already exists; choose a new directory")
    files = list(source_files(root))
    if not (root / "dependencies.lock").is_file():
        parser.error("Missing dependencies.lock: resolve project dependencies first")
    print(f"{'DRY RUN' if args.dry_run else 'COPY'}: {len(files)} files -> {destination}")
    if not args.dry_run:
        destination.mkdir(parents=True, exist_ok=False)
    for path in files:
        relative = path.relative_to(root)
        print(relative.as_posix())
        if not args.dry_run:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


if __name__ == "__main__":
    main()
