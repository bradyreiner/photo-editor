ALTER TABLE canonical_edit_recipes_v13
ADD COLUMN IF NOT EXISTS provenance_payload JSONB NOT NULL
DEFAULT '{"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]}'::jsonb;

ALTER TABLE canonical_edit_recipes_v13
ADD CONSTRAINT canonical_edit_recipes_v13_provenance_schema_check
CHECK (
    jsonb_typeof(provenance_payload) = 'object'
    AND provenance_payload->>'schema' = 'photo-editor-edit-recipe-provenance:v1'
    AND jsonb_typeof(provenance_payload->'presetApplications') = 'array'
    AND provenance_payload - 'schema' - 'presetApplications' = '{}'::jsonb
);
