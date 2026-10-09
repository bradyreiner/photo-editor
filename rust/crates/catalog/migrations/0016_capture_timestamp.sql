-- Keep the exact local EXIF time and optional source offset. A text value
-- preserves local timestamps whose camera timezone is unknown; existing rows
-- remain NULL rather than acquiring an assumed UTC offset.
ALTER TABLE assets
    ADD COLUMN capture_timestamp TEXT;
