-- Reviewed schema snapshot.
-- Runtime schema changes must be added through rust/crates/catalog/migrations.

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
    capture_timestamp TEXT,
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
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CONSTRAINT assets_capture_date_range_check CHECK (
        capture_year IS NOT NULL
        AND capture_month IS NOT NULL
        AND capture_day IS NOT NULL
        AND capture_year BETWEEN 1 AND 9999
        AND capture_month BETWEEN 1 AND 12
        AND capture_day BETWEEN 1 AND 31
    ),
    CONSTRAINT assets_iso_range_check CHECK (iso IS NULL OR iso > 0),
    CONSTRAINT assets_aperture_range_check CHECK (
        aperture IS NULL
        OR (aperture > 0 AND aperture < 'Infinity'::DOUBLE PRECISION)
    ),
    CONSTRAINT assets_rating_range_check CHECK (rating BETWEEN 0 AND 5)
);

CREATE INDEX IF NOT EXISTS idx_assets_capture_date
    ON assets(capture_year, capture_month, capture_day);

CREATE INDEX IF NOT EXISTS idx_assets_camera
    ON assets(camera_make, camera_model);

CREATE INDEX IF NOT EXISTS idx_assets_drive_id
    ON assets(drive_id);

CREATE INDEX IF NOT EXISTS idx_assets_created_at
    ON assets(created_at DESC);

CREATE TABLE IF NOT EXISTS edit_recipes (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id),
    recipe_json JSONB NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS canonical_edit_recipes_v13 (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
    recipe_payload TEXT NOT NULL CHECK(length(btrim(recipe_payload)) > 0),
    cache_key TEXT NOT NULL CHECK(length(btrim(cache_key)) > 0),
    provenance_payload JSONB NOT NULL DEFAULT
        '{"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]}'::jsonb
        CHECK (COALESCE(
            jsonb_typeof(provenance_payload) = 'object'
            AND jsonb_typeof(provenance_payload->'presetApplications') = 'array'
            AND (
                (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v1'
                 AND provenance_payload - 'schema' - 'presetApplications' = '{}'::jsonb)
                OR
                (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v2'
                 AND provenance_payload - 'schema' - 'presetApplications' - 'history' = '{}'::jsonb
                 AND jsonb_typeof(provenance_payload->'history') = 'object'
                 AND jsonb_typeof(provenance_payload->'history'->'activeBranchId') = 'string'
                 AND length(btrim(provenance_payload->'history'->>'activeBranchId')) > 0
                 AND jsonb_typeof(provenance_payload->'history'->'snapshots') = 'array'
                 AND (provenance_payload->'history') - 'activeBranchId' - 'snapshots' = '{}'::jsonb)
            ), false)),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS recipe_migration_records (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
    source_recipe_version INTEGER NOT NULL CHECK(source_recipe_version > 0),
    source_renderer TEXT NOT NULL,
    target_renderer TEXT NOT NULL,
    profile_identities JSONB NOT NULL DEFAULT '[]'::jsonb
        CHECK(jsonb_typeof(profile_identities) = 'array'),
    plan_hash TEXT NOT NULL CHECK(length(btrim(plan_hash)) > 0),
    evidence_hash TEXT NOT NULL CHECK(length(btrim(evidence_hash)) > 0),
    approval_time TIMESTAMPTZ,
    rollback_pointer TEXT NOT NULL CHECK(length(btrim(rollback_pointer)) > 0),
    status TEXT NOT NULL CHECK(status IN ('candidate', 'accepted', 'rejected', 'rolled_back')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CHECK(status <> 'accepted' OR approval_time IS NOT NULL)
);

CREATE TABLE IF NOT EXISTS export_jobs (
    job_id TEXT PRIMARY KEY,
    job_kind TEXT NOT NULL DEFAULT 'asset_export',
    asset_id TEXT NOT NULL DEFAULT '',
    media_type TEXT NOT NULL DEFAULT '',
    display_label TEXT NOT NULL DEFAULT '',
    source_path TEXT NOT NULL DEFAULT '',
    edited_preview_source TEXT NOT NULL DEFAULT '',
    recipe_json JSONB,
    metadata_json JSONB,
    preset_name TEXT NOT NULL,
    destination_path TEXT NOT NULL,
    output_path TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL,
    error_message TEXT NOT NULL DEFAULT '',
    progress INTEGER NOT NULL DEFAULT 0 CHECK(progress BETWEEN 0 AND 100),
    asset_ids JSONB NOT NULL DEFAULT '[]'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS projects (
    name TEXT PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS project_assets (
    project_name TEXT NOT NULL REFERENCES projects(name) ON DELETE CASCADE,
    asset_id TEXT NOT NULL REFERENCES assets(asset_id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (project_name, asset_id)
);

CREATE INDEX IF NOT EXISTS idx_project_assets_asset_id
    ON project_assets(asset_id);

CREATE TABLE IF NOT EXISTS asset_review_states (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
    rating INTEGER NOT NULL DEFAULT 0 CHECK(rating BETWEEN 0 AND 5),
    picked BOOLEAN NOT NULL DEFAULT FALSE,
    rejected BOOLEAN NOT NULL DEFAULT FALSE,
    color_label TEXT NOT NULL DEFAULT '',
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CHECK(NOT (picked AND rejected))
);

CREATE TABLE IF NOT EXISTS asset_relinks (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
    relinked_file_path TEXT NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS timeline_documents (
    timeline_id TEXT PRIMARY KEY,
    asset_id TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL DEFAULT '',
    document_path TEXT NOT NULL DEFAULT '',
    render_path TEXT NOT NULL DEFAULT '',
    poster_path TEXT NOT NULL DEFAULT '',
    duration_seconds DOUBLE PRECISION NOT NULL DEFAULT 0 CHECK(
        duration_seconds >= 0
        AND duration_seconds < 'Infinity'::DOUBLE PRECISION
    ),
    document_json JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_timeline_documents_updated_at
    ON timeline_documents(updated_at DESC);

CREATE TABLE IF NOT EXISTS carousel_documents (
    document_id TEXT PRIMARY KEY,
    name TEXT NOT NULL DEFAULT '',
    document_format TEXT NOT NULL DEFAULT '4x5',
    slide_count INTEGER NOT NULL DEFAULT 1 CHECK(slide_count >= 0),
    document_json JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_carousel_documents_updated_at
    ON carousel_documents(updated_at DESC);

CREATE TABLE IF NOT EXISTS asset_metadata (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
    metadata_json JSONB NOT NULL DEFAULT '{}'::jsonb CHECK (jsonb_typeof(metadata_json) = 'object'),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
