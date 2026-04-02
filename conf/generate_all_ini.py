# This script generates .ini files from .spec files found in a given directory tree.
# It uses the spec_to_ini.py script to perform the conversion for each .spec file.
# The generated .ini files are stored in a 'generated_ini' directory, preserving the original directory structure.
# Usage: python generate_all_ini.py [root_directory] -- If no root_directory is provided, the current directory is used.

import subprocess
import sys
from pathlib import Path

SPEC_EXT = ".spec"
INI_EXT = ".ini"
OUTPUT_ROOT = Path("generated_ini")

def main():
    # Use current directory if no argument is provided
    if len(sys.argv) > 1:
        root = Path(sys.argv[1]).resolve()
    else:
        root = Path.cwd()

    script = Path(__file__).parent / "spec_to_ini.py"

    if not script.exists():
        print("spec_to_ini.py not found next to this script")
        sys.exit(2)

    for spec_file in root.rglob("*" + SPEC_EXT):
        relative_path = spec_file.relative_to(root)
        output_ini = OUTPUT_ROOT / relative_path.with_suffix(INI_EXT)

        output_ini.parent.mkdir(parents=True, exist_ok=True)

        print("Generating:", output_ini)

        result = subprocess.run(
            [
                sys.executable,
                str(script),
                str(spec_file),
                str(output_ini),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )

        if result.returncode != 0:
            print("❌ Failed for", spec_file)
            print(result.stderr)
            sys.exit(result.returncode)

    print("✅ All .ini files generated successfully")

if __name__ == "__main__":
    main()

