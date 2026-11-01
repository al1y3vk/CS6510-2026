#!/usr/bin/env bash
# Recreate the three topics, empty, and drop the consumer groups' offsets. Run before every test run,
# right after db/reset.sh: events left from an earlier run would otherwise be applied to the fresh DBs.
# Needs the broker running (docker compose up -d broker); waits for it to answer.
set -euo pipefail
KAFKA="docker exec kafka /opt/kafka/bin"
SERVER="--bootstrap-server localhost:9092"
TOPICS='catalog\.items|sales\.completed|items\.scanned'
create() { $KAFKA/kafka-topics.sh $SERVER --create "$@" 2>&1 | grep -v '^WARNING: Due to limitations'; }

until $KAFKA/kafka-topics.sh $SERVER --list >/dev/null 2>&1; do sleep 0.5; done

$KAFKA/kafka-topics.sh $SERVER --delete --if-exists --topic "$TOPICS"
$KAFKA/kafka-consumer-groups.sh $SERVER --delete --group inventory --group analytics >/dev/null 2>&1 || true
while $KAFKA/kafka-topics.sh $SERVER --list | grep -qxE "$TOPICS"; do sleep 0.2; done   # deletion is async

# catalog.items: compacted = Kafka keeps at least the newest event per key forever ("items" -> catalog).
create --topic catalog.items --partitions 1 --config cleanup.policy=compact
# One partition each: one consumer per topic, and analytics windows over scans in arrival order.
create --topic sales.completed --partitions 1
create --topic items.scanned --partitions 1
echo "broker reset: catalog.items, sales.completed, items.scanned"
