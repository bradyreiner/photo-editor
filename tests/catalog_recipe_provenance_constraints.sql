-- Integration check; execute only in a task-owned imported proof catalog.
-- All updates roll back, including the successful v1 compatibility check.
BEGIN;
DO $$
DECLARE
  proof_asset TEXT;
  bad JSONB;
  rejected BOOLEAN;
BEGIN
  SELECT asset_id INTO STRICT proof_asset FROM canonical_edit_recipes_v13 LIMIT 1;
  IF NOT EXISTS (SELECT 1 FROM _sqlx_migrations WHERE version = 14 AND success) THEN
    RAISE EXCEPTION 'history migration 14 is not applied';
  END IF;
  UPDATE canonical_edit_recipes_v13 SET provenance_payload =
    '{"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]}'::jsonb
    WHERE asset_id = proof_asset;
  UPDATE canonical_edit_recipes_v13 SET provenance_payload =
    '{"schema":"photo-editor-edit-recipe-provenance:v2","presetApplications":[],"history":{"activeBranchId":"review","snapshots":[]}}'::jsonb
    WHERE asset_id = proof_asset;
  FOR bad IN SELECT value FROM jsonb_array_elements('[
    {"schema":"photo-editor-edit-recipe-provenance:v2","presetApplications":[]},
    {"schema":"photo-editor-edit-recipe-provenance:v2","presetApplications":[],"history":null},
    {"schema":"photo-editor-edit-recipe-provenance:v2","presetApplications":[],"history":{"activeBranchId":"","snapshots":[]}},
    {"schema":"photo-editor-edit-recipe-provenance:v2","presetApplications":[],"history":{"activeBranchId":"main","snapshots":{}}},
    {"schema":"photo-editor-edit-recipe-provenance:v2","presetApplications":[],"history":{"activeBranchId":"main","snapshots":[],"extra":true}},
    {"schema":"photo-editor-edit-recipe-provenance:v1"},
    {"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[],"history":{}}
  ]'::jsonb) LOOP
    rejected := false;
    BEGIN
      UPDATE canonical_edit_recipes_v13 SET provenance_payload = bad WHERE asset_id = proof_asset;
    EXCEPTION WHEN check_violation THEN rejected := true;
    END;
    IF NOT rejected THEN RAISE EXCEPTION 'invalid provenance was accepted: %', bad; END IF;
  END LOOP;
  RAISE NOTICE 'v1/v2 accepted; seven malformed provenance shapes rejected';
END $$;
ROLLBACK;
