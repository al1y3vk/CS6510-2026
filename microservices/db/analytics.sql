-- Analytics service's own database: persisted popular-items snapshots.
DROP TABLE IF EXISTS popular_snapshots CASCADE;
\ir common.sql

CREATE TABLE popular_snapshots (
    id           BIGSERIAL PRIMARY KEY,
    window_start BIGINT      NOT NULL,
    window_end   BIGINT      NOT NULL,
    computed_at  TIMESTAMPTZ NOT NULL,
    items        JSONB       NOT NULL
);
