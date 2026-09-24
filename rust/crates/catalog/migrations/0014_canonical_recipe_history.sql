-- Preserve non-rendering recipe history without changing v13 render/cache identity.
-- Existing v1 provenance remains valid and is never rewritten by this migration.
ALTER TABLE canonical_edit_recipes_v13
DROP CONSTRAINT canonical_edit_recipes_v13_provenance_schema_check;

ALTER TABLE canonical_edit_recipes_v13
ADD CONSTRAINT canonical_edit_recipes_v13_provenance_schema_check
CHECK (COALESCE(
    jsonb_typeof(provenance_payload) = 'object'
    AND jsonb_typeof(provenance_payload->'presetApplications') = 'array'
    AND (
        (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v1'
         AND provenance_payload - 'schema' - 'presetApplications' = '{}'::jsonb)
        OR
        (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v2'
         AND provenance_payload - 'schema' - 'presetApplications' - 'history' = '{}'::jsonb
         AND jsonb_typeof(provenance_payload->'history') = 'object'
         AND jsonb_typeof(provenance_payload->'history'->'activeBranchId') = 'string'
         AND length(btrim(provenance_payload->'history'->>'activeBranchId')) > 0
         AND jsonb_typeof(provenance_payload->'history'->'snapshots') = 'array'
         AND (provenance_payload->'history') - 'activeBranchId' - 'snapshots' = '{}'::jsonb)
    ), false));
