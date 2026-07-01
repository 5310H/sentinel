#!/bin/bash
for f in dispatcher.c ha_mqtt.c net.c noonlight.c; do
  echo "$f:"
  echo "  engine: $(git log -1 --format="%ai" -- components/engine/$f)"
  echo "  comms:  $(git log -1 --format="%ai" -- components/comms/$f)"
done
