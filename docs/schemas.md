# Core Schemas

## CatalogAssetRecord

- `asset_id`
- `metadata.capture_year`
- `metadata.capture_month`
- `metadata.capture_day`
- `metadata.camera_make`
- `metadata.camera_model`
- `import_mode`
- `location`

## CaptureMetadata

- `capture_year`
- `capture_month`
- `capture_day`
- `camera_make`
- `camera_model`
- `lens_model`
- `iso`
- `shutter_speed`
- `aperture`

## AssetLocation

- `drive_id`
- `root_path`
- `relative_path`
- `online`

## EditRecipe

- `asset_id`
- `global_adjustments[]`
- `mask_stack[]`

## MaskDefinition

- `id`
- `kind`
- `ai_assisted`

## ExportJob

- `job_id`
- `asset_ids[]`
- `preset_name`
- `destination`
- `status`

## ManagedPathTemplate

Default template:

`YYYY/MM/DD/Camera Model`
