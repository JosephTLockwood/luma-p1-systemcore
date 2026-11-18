#!/bin/sh
# Luma P1: keep the FRC robot address 10.TE.AM.2/24 on eth0, next to whatever dhcpcd assigns.
#
# On a direct Ethernet link the P1 only has a link-local 169.254.x.x address, and Limelight's
# WebRTC (H.264 "UDP" stream) never offers link-local candidates, so the browser falls back to
# MJPEG. A normal private address fixes that, and 10.TE.AM.2 is where the Driver Station
# expects a robot anyway. Follows /etc/team_number.txt (set from the SystemCore dashboard).
IF=${P1_TEAMIP_IF:-eth0}
CUR=""
cleanup() { [ -n "$CUR" ] && ip addr del "$CUR" dev "$IF" 2>/dev/null; exit 0; }
trap cleanup TERM INT

while true; do
  T=$(tr -cd 0-9 < /etc/team_number.txt 2>/dev/null)
  WANT=""
  [ -n "$T" ] && [ "$T" -gt 0 ] && WANT="10.$((T / 100)).$((T % 100)).2/24"
  if [ -n "$CUR" ] && [ "$CUR" != "$WANT" ]; then
    ip addr del "$CUR" dev "$IF" 2>/dev/null && echo "removed $CUR (team number changed)"
  fi
  if [ -n "$WANT" ] && ! ip -4 addr show dev "$IF" | grep -q "inet ${WANT%/*}/"; then
    ip addr add "$WANT" dev "$IF" 2>/dev/null && echo "added $WANT on $IF"
  fi
  CUR=$WANT
  sleep 10 &
  wait $!
done
