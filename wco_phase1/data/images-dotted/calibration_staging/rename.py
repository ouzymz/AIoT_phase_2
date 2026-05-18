import os
import re

def rename_calib_files(folder_path, t=0, p=0, c=0, dry_run=True):
    """
    Renames calib_XXX.jpg files to t{t}_p{p}_c{c}_XXX.jpg

    Args:
        folder_path (str): Path to the folder containing the images
        t (int): Turbidity label (0 or 1)
        p (int): Particle content label (0 or 1)
        c (int): Water contamination label (0 or 1)
        dry_run (bool): If True, only previews changes without renaming
    """
    pattern = re.compile(r'^calib_(\d+)\.jpg$', re.IGNORECASE)

    if dry_run:
        print("🔍 DRY RUN — no files will be changed:\n")

    renamed = 0
    for filename in sorted(os.listdir(folder_path)):
        match = pattern.match(filename)
        if match:
            number = match.group(1)
            new_name = f"t{t}_p{p}_c{c}_{number}.jpg"
            print(f"  {filename}  →  {new_name}")
            if not dry_run:
                old_path = os.path.join(folder_path, filename)
                new_path = os.path.join(folder_path, new_name)
                os.rename(old_path, new_path)
            renamed += 1

    label = "previewed" if dry_run else "renamed"
    print(f"\n✅ {renamed} file(s) {label}.")

    # Preview first (safe)
# rename_calib_files("./", t=0, p=0, c=0, dry_run=True)
# Then apply for real
rename_calib_files("./", t=0, p=0, c=0, dry_run=False)