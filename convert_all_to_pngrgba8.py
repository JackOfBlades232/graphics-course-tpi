# vibecode extravaganza danza
# maybe i should start making tools with this level of bells and whistles myself

from pathlib import Path
import sys
 
try:
    from PIL import Image
except ImportError:
    print("Pillow is not installed. Run:  pip install Pillow")
    sys.exit(1)
 
FOLDER = Path(sys.argv[1])

def collect_unique(patterns: list[str], folder: Path) -> list[Path]:
    seen: set[Path] = set()
    result: list[Path] = []
    for pattern in patterns:
        for f in folder.glob(pattern):
            key = f.resolve()
            if key not in seen:
                seen.add(key)
                result.append(f)
    return result

def convert_jpgs(folder: Path) -> None:
    jpg_files = collect_unique(["*.jpg", "*.jpeg", "*.JPG", "*.JPEG"], folder)

    if not jpg_files:
        print("No JPG/JPEG files found.")
        return

    print(f"── JPG → PNG ({len(jpg_files)} file(s)) ──────────────────────────")

    for jpg_path in jpg_files:
        png_path = jpg_path.with_suffix(".png")
        try:
            with Image.open(jpg_path) as img:
                img.save(png_path, "PNG")
            jpg_path.unlink()
            print(f"  ✓  {jpg_path.name}  →  {png_path.name}")
        except Exception as e:
            print(f"  ✗  {jpg_path.name}  ERROR: {e}")

def add_alpha_to_pngs(folder: Path) -> None:
    png_files = collect_unique(["*.png", "*.PNG"], folder)

    if not png_files:
        print("\nNo PNG files found for alpha processing.")
        return

    print(f"\n── Alpha channel pass ({len(png_files)} PNG(s)) ──────────────────")

    for png_path in png_files:
        try:
            with Image.open(png_path) as img:
                if img.mode == "RGB":
                    rgba = img.convert("RGBA")
                    rgba.save(png_path, "PNG")
                    print(f"  ✓  {png_path.name}  RGB8 → RGBA (alpha=255 added)")
                else:
                    print(f"  –  {png_path.name}  {img.mode} (skipped)")
        except Exception as e:
            print(f"  ✗  {png_path.name}  ERROR: {e}")

def main() -> None:
    if not FOLDER.is_dir():
        print(f"Error: FOLDER not found: {FOLDER}")
        sys.exit(1)
 
    print(f"Folder: {FOLDER}\n")
    convert_jpgs(FOLDER)
    add_alpha_to_pngs(FOLDER)
    print("\nAll done.")
 
 
if __name__ == "__main__":
    main()
