CREATE TABLE IF NOT EXISTS timeline_documents (
    timeline_id TEXT PRIMARY KEY,
    asset_id TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL DEFAULT '',
    document_path TEXT NOT NULL DEFAULT '',
    render_path TEXT NOT NULL DEFAULT '',
    poster_path TEXT NOT NULL DEFAULT '',
    duration_seconds DOUBLE PRECISION NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_timeline_documents_updated_at
    ON timeline_documents (updated_at DESC);
