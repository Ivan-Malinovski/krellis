#!/usr/bin/env bash
# Decoration feedback with resize glides and camera flights off.
export EXPECT_FEEDBACK_ONLY=1
exec bash "$KRELLIS_ROOT/scenarios/motion-polish.sh"
