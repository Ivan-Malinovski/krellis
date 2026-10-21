#!/usr/bin/env bash
# The overlays take every colour from the Theme tokens (effect/theme_tokens.h). A literal colour in a QML file would
# ignore the user's colour scheme, so none may appear. "transparent" is not a colour choice and is allowed.
set -u
dir=${1:?usage: check_qml_colors.sh <effect dir>}
hits=$(grep -nE '#[0-9a-fA-F]{3,8}\b|Qt\.(rgba|hsla|hsva|rgb)\(|"(white|black|red|green|blue|gray|grey|yellow|orange)"|color: *[a-z]+ *$' "$dir"/*.qml | grep -v 'import ' || true)
if [ -n "$hits" ]; then
    echo "colour literals in QML (use the theme tokens):"
    echo "$hits"
    exit 1
fi
