# Sourced helper. Resolves the pinned Wine fetched by fetch-wine.sh.
#
#   at_resolve_wine_pin <wine-dir>    -> sets AT_WINE_PIN to the extracted root
#                                        (has usr/ + bin/), or "" for system Wine.
#   <wine-dir> is $AT_HOME/wine (default .atzc/wine).
#
# Honors AT_WINE_ROOT (explicit override) and AT_NO_WINE_PIN=1 (force system).
# Consumers use:  $AT_WINE_PIN/bin/wine            (loader)
#                 $AT_WINE_PIN/usr/include/wine/windows   (SDK headers)
#                 $AT_WINE_PIN/usr/lib/wine/i386-windows  (builtin PE DLLs)
at_resolve_wine_pin() {
  local wine_dir="$1"
  if [[ "${AT_NO_WINE_PIN:-0}" == 1 ]]; then AT_WINE_PIN=""; return 0; fi
  if [[ -n "${AT_WINE_ROOT:-}" ]]; then AT_WINE_PIN="$AT_WINE_ROOT"; return 0; fi
  local cur="$wine_dir/current"
  if [[ -x "$cur/bin/wine" ]]; then AT_WINE_PIN="$cur"; else AT_WINE_PIN=""; fi
}
