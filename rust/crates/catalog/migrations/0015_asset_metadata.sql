CREATE TABLE IF NOT EXISTS asset_metadata (
  asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
  metadata_json JSONB NOT NULL DEFAULT '{}'::jsonb CHECK (jsonb_typeof(metadata_json) = 'object'),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
