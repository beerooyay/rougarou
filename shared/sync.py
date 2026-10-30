from pathlib import Path

root = Path(__file__).resolve().parent.parent
source = (root / "shared/routes.json").read_bytes()
for target in (root / "godot/routes.json", root / "unity/Assets/Resources/routes.json", root / "unreal/Content/Data/routes.json"):
    target.write_bytes(source)
