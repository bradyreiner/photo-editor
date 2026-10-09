#!/usr/bin/env python3
"""Run the native Edit canvas workflow against a disposable socket-only catalog."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver", required=True, type=Path)
    parser.add_argument("--postgres-runtime", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--boards", action="store_true", help="Verify integrated Edit boards as well")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="looksy-canvas-pg-", dir="/private/tmp"))
    pg_bin = args.postgres_runtime.resolve() / "bin"
    env = os.environ.copy()
    env["DYLD_FALLBACK_LIBRARY_PATH"] = str(args.postgres_runtime.resolve() / "lib")
    source_paths = ["apps/desktop/qml/Main.qml", "apps/desktop/qml/workspaces/StudioWorkspace.qml",
                    "apps/desktop/qml/components/MenuButton.qml", "apps/desktop/qml/components/LayerRowActionZone.qml",
                    "apps/desktop/qml/components/LayerRowChrome.qml", "apps/desktop/qml/components/LayerActionsMenu.qml", "apps/desktop/qml/components/LayerTransformOverlay.qml",
                    "apps/desktop/qml/workspaces/CarouselView.qml", "apps/desktop/qml/components/SavedBoardsLibrary.qml",
                    "apps/desktop/qml/components/StudioImageViewport.qml",
                    "apps/desktop/src/editor_preview_controller.cpp", "apps/desktop/src/editor_preview_controller.h",
                    "apps/desktop/src/editor_recipe_save_journal.cpp",
                    "apps/desktop/src/carousel_document_controller.cpp", "apps/desktop/src/carousel_document_controller.h"]
    def digest(path):
        with Path(path).open("rb") as file:
            return hashlib.file_digest(file, "sha256").hexdigest()
    source_hashes = {path: digest(path) for path in source_paths}
    receipt = {"accepted": False, "catalog_fixture": str(root), "source_sha256": source_hashes,
               "driver_sha256": digest(args.driver.resolve())}
    started = False
    try:
        subprocess.run([str(pg_bin / "initdb"), "-D", str(root / "data"), "-A", "trust", "--no-locale", "-U", "canvas_test"],
                       env=env, check=True, capture_output=True, text=True)
        subprocess.run([str(pg_bin / "pg_ctl"), "-D", str(root / "data"), "-l", str(root / "postgres.log"),
                        "-o", f"-k {root} -p 55457 -c listen_addresses=''", "-w", "start"], env=env, check=True, capture_output=True, text=True)
        started = True
        env.update(PHOTO_EDITOR_CANVAS_PROOF_DATABASE_URL=f"postgresql://canvas_test@localhost/postgres?host={root}&port=55457",
                   PHOTO_EDITOR_RUN_MONOLITHIC_SMOKE="1", PHOTO_EDITOR_SMOKE_STOP_AFTER_STUDIO_POLISH="1",
                   PHOTO_EDITOR_SMOKE_STOP_AFTER_BLANK_CANVAS="1", PHOTO_EDITOR_VISUAL_PROOF_DIR=str(output),
                   PHOTO_EDITOR_SMOKE_NATIVE_TEXTURE="1", PHOTO_EDITOR_SMOKE_NATIVE_POINTER="1",
                   QT_QPA_PLATFORM="cocoa", QSG_RHI_BACKEND="metal")
        if args.boards:
            env["PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_BOARDS"] = "1"
        with (output / "native.log").open("w") as log:
            result = subprocess.run([str(args.driver.resolve())], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=240)
        receipt["native_exit_code"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Native workflow failed; see {output / 'native.log'}")
        query = subprocess.run([str(pg_bin / "psql"), "-h", str(root), "-p", "55457", "-U", "canvas_test", "-d", "postgres",
                                "-X", "-A", "-t", "-v", "ON_ERROR_STOP=1", "-c",
                                "SELECT count(*) FROM assets;"], env=env, check=True, capture_output=True, text=True)
        receipt["persisted_original_photo_assets"] = int(query.stdout.strip())
        if receipt["persisted_original_photo_assets"] < 1:
            raise RuntimeError("The original photo was not persisted in the real catalog")
        recipe = subprocess.run([str(pg_bin / "psql"), "-h", str(root), "-p", "55457", "-U", "canvas_test", "-d", "postgres",
                                 "-X", "-A", "-t", "-v", "ON_ERROR_STOP=1", "-c",
                                 "SELECT e.recipe_json::text FROM edit_recipes e JOIN canonical_edit_recipes_v13 c "
                                 "ON c.asset_id=e.asset_id ORDER BY c.updated_at DESC LIMIT 1;"],
                                env=env, check=True, capture_output=True, text=True)
        persisted = json.loads(recipe.stdout.strip())
        expected = json.loads((output / "original-photo-recipe.json").read_text())
        receipt["persisted_recipe_matches"] = persisted == expected
        if not receipt["persisted_recipe_matches"]:
            raise RuntimeError("The original photo recipe changed while editing a separate board document")
        (output / "catalog-recipe.json").write_text(json.dumps(persisted, indent=2) + "\n")
        if args.boards:
            board = subprocess.run([str(pg_bin / "psql"), "-h", str(root), "-p", "55457", "-U", "canvas_test", "-d", "postgres",
                                    "-X", "-A", "-t", "-v", "ON_ERROR_STOP=1", "-c",
                                    "SELECT document_json::text FROM carousel_documents ORDER BY updated_at DESC LIMIT 1;"],
                                   env=env, check=True, capture_output=True, text=True)
            persisted_board = json.loads(board.stdout.strip())
            (output / "catalog-board.json").write_text(json.dumps(persisted_board, indent=2) + "\n")
            receipt["board_persisted"] = len(persisted_board.get("slides", [])) == 3
            boards = persisted_board.get("slides", [])
            groups = persisted_board.get("groups", [])
            shared = next((group for group in groups if group.get("editSpread") and group.get("startIndex") == 1), {})
            second_layers = shared.get("media", {}).get("adjustmentPatch", {}).get("adjustmentState", {}).get("photoLayers", [])
            receipt["shared_image_persisted"] = shared.get("span") == 2 and all(
                boards[index].get("linkedGroupId") == shared.get("id") for index in (1, 2))
            if not receipt["shared_image_persisted"]:
                raise RuntimeError("The extended image and live shared recipe were not persisted")
            receipt["board_graphics_persisted"] = len(second_layers) == 3 and any(
                layer.get("kind") == "text" and layer.get("text") == "Second board" for layer in second_layers)
            receipt["board_brush_pixels_persisted"] = any(
                layer.get("kind") == "pixel" and layer.get("paintTiles", {}).get("tiles") for layer in second_layers)
            board_ids = [board.get("editBoardId", "") for board in boards]
            receipt["independent_board_ids"] = all(board_ids) and len(set(board_ids)) == len(board_ids)
            receipt["extension_persisted"] = receipt["shared_image_persisted"]
            receipt["extension_neighbor_identity_persisted"] = receipt["shared_image_persisted"]
            receipt["board_name_persisted"] = boards[1].get("boardName", "") == "Detail board"
            if not receipt["extension_neighbor_identity_persisted"] or not receipt["board_name_persisted"]:
                raise RuntimeError("The board name or original continuation neighbor was not persisted")
            if not receipt["independent_board_ids"] or not receipt["extension_persisted"]:
                raise RuntimeError("Board identity or adjoining-canvas state was not persisted")
            if not receipt["board_graphics_persisted"]:
                raise RuntimeError("The second board's editable shapes and text were not persisted")
            if not receipt["board_brush_pixels_persisted"]:
                raise RuntimeError("The second board's native brush pixels were not persisted")
            if not receipt["board_persisted"]:
                raise RuntimeError("Converted board document was not saved in the catalog")
        receipt["source_unchanged_during_run"] = all(digest(path) == value for path, value in source_hashes.items())
        if not receipt["source_unchanged_during_run"]:
            raise RuntimeError("Source changed during the proof; rerun against a stable build")
        receipt["accepted"] = True
    except Exception as error:
        receipt["error"] = str(error)
        raise
    finally:
        if started:
            stopped = subprocess.run([str(pg_bin / "pg_ctl"), "-D", str(root / "data"), "-m", "fast", "-w", "stop"],
                                     env=env, capture_output=True, text=True)
            receipt["catalog_stopped"] = stopped.returncode == 0
        (output / "catalog-proof.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
