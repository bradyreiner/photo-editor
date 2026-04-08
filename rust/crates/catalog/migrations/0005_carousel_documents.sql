CREATE TABLE IF NOT EXISTS carousel_documents (
    document_id TEXT PRIMARY KEY,
    name TEXT NOT NULL DEFAULT '',
    document_format TEXT NOT NULL DEFAULT '4x5',
    slide_count INTEGER NOT NULL DEFAULT 1,
    document_json JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_carousel_documents_updated_at
    ON carousel_documents (updated_at DESC);
