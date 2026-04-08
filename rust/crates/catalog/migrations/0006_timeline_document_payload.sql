ALTER TABLE timeline_documents
    ADD COLUMN IF NOT EXISTS document_json JSONB NOT NULL DEFAULT '{}'::jsonb;
