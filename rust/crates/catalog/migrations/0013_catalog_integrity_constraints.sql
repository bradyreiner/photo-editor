-- Existing rows remain readable so the application can report the corrupt record
-- identity and guide recovery. PostgreSQL still enforces NOT VALID constraints for
-- every new or updated row; release validation must validate them after remediation.
ALTER TABLE assets
    ADD CONSTRAINT assets_capture_date_range_check
    CHECK (
        capture_year IS NOT NULL
        AND capture_month IS NOT NULL
        AND capture_day IS NOT NULL
        AND capture_year BETWEEN 1 AND 9999
        AND capture_month BETWEEN 1 AND 12
        AND capture_day BETWEEN 1 AND 31
    ) NOT VALID;

ALTER TABLE assets
    ADD CONSTRAINT assets_iso_range_check
    CHECK (iso IS NULL OR iso > 0) NOT VALID;

ALTER TABLE assets
    ADD CONSTRAINT assets_aperture_range_check
    CHECK (
        aperture IS NULL
        OR (aperture > 0 AND aperture < 'Infinity'::DOUBLE PRECISION)
    ) NOT VALID;

ALTER TABLE assets
    ADD CONSTRAINT assets_rating_range_check
    CHECK (rating BETWEEN 0 AND 5) NOT VALID;

ALTER TABLE asset_review_states
    ADD CONSTRAINT asset_review_states_rating_range_check
    CHECK (rating BETWEEN 0 AND 5) NOT VALID;

ALTER TABLE asset_review_states
    ADD CONSTRAINT asset_review_states_flag_exclusivity_check
    CHECK (NOT (picked AND rejected)) NOT VALID;

ALTER TABLE export_jobs
    ADD CONSTRAINT export_jobs_progress_range_check
    CHECK (progress BETWEEN 0 AND 100) NOT VALID;

ALTER TABLE timeline_documents
    ADD CONSTRAINT timeline_documents_duration_range_check
    CHECK (
        duration_seconds >= 0
        AND duration_seconds < 'Infinity'::DOUBLE PRECISION
    ) NOT VALID;

ALTER TABLE carousel_documents
    ADD CONSTRAINT carousel_documents_slide_count_range_check
    CHECK (slide_count >= 0) NOT VALID;

ALTER TABLE recipe_migration_records
    ADD CONSTRAINT recipe_migration_records_profile_identities_array_check
    CHECK (jsonb_typeof(profile_identities) = 'array') NOT VALID;
