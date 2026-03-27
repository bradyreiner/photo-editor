CREATE TABLE IF NOT EXISTS drives (
    drive_id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    mount_path TEXT NOT NULL,
    is_online BOOLEAN NOT NULL DEFAULT TRUE,
    last_seen_at TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS assets (
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
    aperture DOUBLE PRECISION,
    rating INTEGER NOT NULL DEFAULT 0,
    flag_state TEXT NOT NULL DEFAULT 'unflagged',
    color_label TEXT NOT NULL DEFAULT '',
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_assets_capture_date
    ON assets(capture_year, capture_month, capture_day);

CREATE INDEX IF NOT EXISTS idx_assets_camera
    ON assets(camera_make, camera_model);

CREATE TABLE IF NOT EXISTS edit_recipes (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id),
    recipe_json JSONB NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS export_jobs (
    job_id TEXT PRIMARY KEY,
    preset_name TEXT NOT NULL,
    destination_path TEXT NOT NULL,
    status TEXT NOT NULL,
    asset_ids JSONB NOT NULL DEFAULT '[]'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
