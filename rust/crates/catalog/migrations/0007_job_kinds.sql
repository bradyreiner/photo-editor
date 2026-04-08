ALTER TABLE export_jobs
    ADD COLUMN IF NOT EXISTS job_kind TEXT NOT NULL DEFAULT 'asset_export';

ALTER TABLE export_jobs
    ADD COLUMN IF NOT EXISTS metadata_json JSONB;
