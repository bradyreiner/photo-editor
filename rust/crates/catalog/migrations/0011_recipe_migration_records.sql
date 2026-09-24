CREATE TABLE IF NOT EXISTS recipe_migration_records (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
    source_recipe_version INTEGER NOT NULL CHECK(source_recipe_version > 0),
    source_renderer TEXT NOT NULL CHECK(length(btrim(source_renderer)) > 0),
    target_renderer TEXT NOT NULL CHECK(length(btrim(target_renderer)) > 0),
    profile_identities JSONB NOT NULL DEFAULT '[]'::jsonb,
    plan_hash TEXT NOT NULL CHECK(length(btrim(plan_hash)) > 0),
    evidence_hash TEXT NOT NULL CHECK(length(btrim(evidence_hash)) > 0),
    approval_time TIMESTAMPTZ,
    rollback_pointer TEXT NOT NULL CHECK(length(btrim(rollback_pointer)) > 0),
    status TEXT NOT NULL CHECK(status IN ('candidate', 'accepted', 'rejected', 'rolled_back')),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    CHECK(status <> 'accepted' OR approval_time IS NOT NULL)
);
