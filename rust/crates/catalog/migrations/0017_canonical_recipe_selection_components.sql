-- Match the desktop selection (v3) and component (v4) provenance envelopes.
-- Retain all existing recipe/provenance bytes and v1/v2 compatibility.
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
        (provenance_payload->>'schema' IN (
             'photo-editor-edit-recipe-provenance:v2',
             'photo-editor-edit-recipe-provenance:v3',
             'photo-editor-edit-recipe-provenance:v4')
         AND jsonb_typeof(provenance_payload->'history') = 'object'
         AND jsonb_typeof(provenance_payload->'history'->'activeBranchId') = 'string'
         AND length(btrim(provenance_payload->'history'->>'activeBranchId')) > 0
         AND jsonb_typeof(provenance_payload->'history'->'snapshots') = 'array'
         AND (provenance_payload->'history') - 'activeBranchId' - 'snapshots' = '{}'::jsonb
         AND (
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v2'
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' = '{}'::jsonb)
             OR
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v3'
              AND jsonb_typeof(provenance_payload->'selectionState') = 'object'
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' - 'selectionState' = '{}'::jsonb)
             OR
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v4'
              AND jsonb_typeof(provenance_payload->'selectionState') = 'object'
              AND provenance_payload->'requiredCapabilities' = '["smart-object-documents-v1"]'::jsonb
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' - 'selectionState' - 'requiredCapabilities' = '{}'::jsonb)
         ))
    ), false));
