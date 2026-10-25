-- Included by every service schema. Timestamps leave Postgres already formatted; C++ never parses dates.
CREATE OR REPLACE FUNCTION iso8601(ts TIMESTAMPTZ) RETURNS TEXT LANGUAGE sql IMMUTABLE AS $$
    SELECT to_char(ts AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.MS"Z"')
$$;
