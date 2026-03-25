-- Legacy local schema draft kept for reference.
-- The active product-backed schema is postgres-schema.sql.

CREATE TABLE drives (
    drive_id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    mount_path TEXT NOT NULL,
    is_online INTEGER NOT NULL DEFAULT 1,
    last_seen_at TEXT
);

CREATE TABLE assets (
    asset_id TEXT PRIMARY KEY,
    drive_id TEXT NOT NULL REFERENCES drives(drive_id),
    root_path TEXT NOT NULL,
    relative_path TEXT NOT NULL,
    import_mode TEXT NOT NULL CHECK(import_mode IN ('referenced', 'managed')),
    capture_year INTEGER,
    capture_month INTEGER,
    capture_day INTEGER,
    camera_make TEXT,
    camera_model TEXT,
    lens_model TEXT,
    iso INTEGER,
    shutter_speed TEXT,
    aperture REAL,
    rating INTEGER DEFAULT 0,
    flag_state TEXT DEFAULT 'unflagged',
    color_label TEXT DEFAULT '',
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE INDEX idx_assets_capture_date
    ON assets(capture_year, capture_month, capture_day);

CREATE INDEX idx_assets_camera
    ON assets(camera_make, camera_model);

CREATE TABLE edit_recipes (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id),
    recipe_json TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE export_jobs (
    job_id TEXT PRIMARY KEY,
    preset_name TEXT NOT NULL,
    destination_path TEXT NOT NULL,
    status TEXT NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
