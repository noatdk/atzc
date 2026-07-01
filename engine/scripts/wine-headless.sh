# Sourced helper. When AT_HEADLESS=1, keep Wine off the user's compositor:
#   1. Prefer a private Xvfb (MsctfShim/ATOK expect a display for TSF windows).
#   2. Fall back to Wine's null graphics driver when Xvfb is unavailable.
#
#   at_headless_ensure_display   pick Xvfb or null mode; export AT_HEADLESS_MODE
#   at_headless_teardown         stop a prefix-owned Xvfb (e.g. before wineserver -k)

at_headless_lock_file() {
  printf '%s/.at-headless-xvfb' "${AT_WINEPREFIX:-${PREFIX:-}}"
}

at_headless_ensure_display() {
  [[ "${AT_HEADLESS:-0}" == 1 ]] || return 0

  # Never let Wine attach to the real session compositor.
  unset WAYLAND_DISPLAY WAYLAND_SOCKET GDK_BACKEND SDL_VIDEODRIVER

  local lock xvfb_pid display dnum i
  lock=$(at_headless_lock_file)

  if [[ -f "$lock" ]]; then
    # shellcheck disable=SC1090
    source "$lock"
    if [[ "${AT_HEADLESS_MODE:-}" == xvfb && -n "${xvfb_pid:-}" ]] &&
       kill -0 "$xvfb_pid" 2>/dev/null &&
       xdpyinfo -display "${display:?}" >/dev/null 2>&1; then
      export DISPLAY="$display"
      export AT_HEADLESS_MODE=xvfb
      return 0
    fi
    if [[ "${AT_HEADLESS_MODE:-}" == null ]]; then
      unset DISPLAY
      export AT_HEADLESS_MODE=null
      return 0
    fi
    at_headless_teardown 2>/dev/null || true
  fi

  if command -v Xvfb >/dev/null 2>&1 && command -v xdpyinfo >/dev/null 2>&1; then
    dnum=${AT_HEADLESS_DISPLAY_NUM:-}
    if [[ -z "$dnum" ]]; then
      dnum=$((50 + ($$ % 150)))
    fi
    while xdpyinfo -display ":$dnum" >/dev/null 2>&1; do
      dnum=$((dnum + 1))
      [[ "$dnum" -lt 250 ]] || break
    done
    if [[ "$dnum" -lt 250 ]]; then
      display=":$dnum"
      # -noreset: keep the virtual screen alive across warm-reuse daemon-once runs.
      Xvfb "$display" -screen 0 1024x768x24 -nolisten tcp -ac -noreset >/dev/null 2>&1 &
      xvfb_pid=$!
      for i in $(seq 1 100); do
        if xdpyinfo -display "$display" >/dev/null 2>&1; then
          export DISPLAY="$display"
          export AT_HEADLESS_MODE=xvfb
          printf 'AT_HEADLESS_MODE=xvfb\ndisplay=%q\nxvfb_pid=%s\n' \
            "$display" "$xvfb_pid" >"$lock"
          return 0
        fi
        if ! kill -0 "$xvfb_pid" 2>/dev/null; then
          break
        fi
        sleep 0.05
      done
      kill "$xvfb_pid" 2>/dev/null || true
      wait "$xvfb_pid" 2>/dev/null || true
    fi
  fi

  # No working Xvfb — run without any host display (Wine null graphics).
  unset DISPLAY
  export AT_HEADLESS_MODE=null
  printf 'AT_HEADLESS_MODE=null\n' >"$lock"
  return 0
}

at_headless_teardown() {
  local lock xvfb_pid
  lock=$(at_headless_lock_file)
  [[ -f "$lock" ]] || return 0
  # shellcheck disable=SC1090
  source "$lock"
  if [[ "${AT_HEADLESS_MODE:-}" == xvfb && -n "${xvfb_pid:-}" ]]; then
    kill "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
  fi
  rm -f "$lock"
  unset AT_HEADLESS_MODE
}

at_headless_graphics_driver() {
  if [[ "${AT_HEADLESS:-0}" == 1 && "${AT_HEADLESS_MODE:-}" == null ]]; then
    printf '%s' null
  else
    printf '%s' x11
  fi
}
