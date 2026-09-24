CREATE TABLE IF NOT EXISTS canonical_edit_recipes_v13 (
    asset_id TEXT PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
    recipe_payload TEXT NOT NULL CHECK(length(btrim(recipe_payload)) > 0),
    cache_key TEXT NOT NULL CHECK(length(btrim(cache_key)) > 0),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
