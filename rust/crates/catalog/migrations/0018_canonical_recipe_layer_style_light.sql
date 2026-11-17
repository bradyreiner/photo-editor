-- Negotiate linked document-wide Layer Styles lighting alongside the existing
-- desktop v5/v6 provenance contracts. Keep earlier provenance envelopes valid.
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
        OR
        (provenance_payload->>'schema' IN (
             'photo-editor-edit-recipe-provenance:v3',
             'photo-editor-edit-recipe-provenance:v4',
             'photo-editor-edit-recipe-provenance:v5',
             'photo-editor-edit-recipe-provenance:v6',
             'photo-editor-edit-recipe-provenance:v7')
         AND jsonb_typeof(provenance_payload->'history') = 'object'
         AND jsonb_typeof(provenance_payload->'history'->'activeBranchId') = 'string'
         AND length(btrim(provenance_payload->'history'->>'activeBranchId')) > 0
         AND jsonb_typeof(provenance_payload->'history'->'snapshots') = 'array'
         AND (provenance_payload->'history') - 'activeBranchId' - 'snapshots' = '{}'::jsonb
         AND jsonb_typeof(provenance_payload->'selectionState') = 'object'
         AND (
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v3'
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' - 'selectionState' = '{}'::jsonb)
             OR
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v4'
              AND provenance_payload->'requiredCapabilities' = '["smart-object-documents-v1"]'::jsonb
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' - 'selectionState' - 'requiredCapabilities' = '{}'::jsonb)
             OR
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v5'
              AND provenance_payload->'requiredCapabilities' IN (
                  '["component-object-assets-v1"]'::jsonb,
                  '["component-object-assets-v1","smart-object-documents-v1"]'::jsonb)
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' - 'selectionState' - 'requiredCapabilities' = '{}'::jsonb)
             OR
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v6'
              AND provenance_payload->'requiredCapabilities' IN (
                  '["layer-styles-v1"]'::jsonb,
                  '["layer-styles-v1","smart-object-documents-v1"]'::jsonb,
                  '["component-object-assets-v1","layer-styles-v1"]'::jsonb,
                  '["component-object-assets-v1","layer-styles-v1","smart-object-documents-v1"]'::jsonb)
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' - 'selectionState' - 'requiredCapabilities' = '{}'::jsonb)
             OR
             (provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v7'
              AND CASE
                    WHEN jsonb_typeof(provenance_payload->'documentLayerStyleLightAngle') = 'number'
                    THEN (provenance_payload->>'documentLayerStyleLightAngle')::numeric BETWEEN -3600 AND 3600
                    ELSE false
                  END
              AND provenance_payload->'requiredCapabilities' IN (
                  '["document-layer-style-light-v1"]'::jsonb,
                  '["document-layer-style-light-v1","smart-object-documents-v1"]'::jsonb,
                  '["layer-styles-v1","document-layer-style-light-v1"]'::jsonb,
                  '["layer-styles-v1","document-layer-style-light-v1","smart-object-documents-v1"]'::jsonb,
                  '["component-object-assets-v1","document-layer-style-light-v1"]'::jsonb,
                  '["component-object-assets-v1","document-layer-style-light-v1","smart-object-documents-v1"]'::jsonb,
                  '["component-object-assets-v1","layer-styles-v1","document-layer-style-light-v1"]'::jsonb,
                  '["component-object-assets-v1","layer-styles-v1","document-layer-style-light-v1","smart-object-documents-v1"]'::jsonb)
              AND provenance_payload - 'schema' - 'presetApplications' - 'history' - 'selectionState' - 'documentLayerStyleLightAngle' - 'requiredCapabilities' = '{}'::jsonb)
         ))
    ), false));
