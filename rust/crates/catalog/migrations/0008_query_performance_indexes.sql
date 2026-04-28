-- Index on drive_id for JOIN performance in list_assets / list_asset_snapshots.
CREATE INDEX IF NOT EXISTS idx_assets_drive_id
    ON assets(drive_id);

-- Index on created_at for list_asset_snapshots ORDER BY created_at DESC.
CREATE INDEX IF NOT EXISTS idx_assets_created_at
    ON assets(created_at DESC);
