#!/bin/bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP_PROJECT="$ROOT/App.Native.UGreenLED"
[[ -d "$APP_PROJECT" ]] || APP_PROJECT="$ROOT"
API="$APP_PROJECT/app/ui/api.cgi"
TMP=$(mktemp -d)
trap '[[ -n "${FAND_PID:-}" ]] && kill "$FAND_PID" 2>/dev/null || true; rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
assert_contains() { [[ "$1" == *"$2"* ]] || fail "missing [$2] in [$1]"; }

BIN="$TMP/ugreenctl"; FAND="$TMP/ugreenctl-fand"; PLUGINS="$TMP/models"; CALLS="$TMP/calls"; IT87_MODULE="$TMP/it87"
mkdir -p "$PLUGINS" "$TMP/run" "$TMP/var"
cat > "$TMP/var/settings.conf" <<'EOF'
[logging]
level=off

[fan_curve]
enabled=false
EOF
for model in dx4600 dxp4800 dxp4800plus dxp4800s dxp480tplus dxp6800pro dxp6800plus dxp6800; do : > "$PLUGINS/$model.so"; done
cat > "$BIN" <<'EOF'
#!/bin/sh
printf 'CALL' >> "$UGREENCTL_CALLS"
for arg in "$@"; do printf ' <%s>' "$arg" >> "$UGREENCTL_CALLS"; done
printf '\n' >> "$UGREENCTL_CALLS"
case " $* " in
  *" fan status "*) echo 'fan sys: pwm=64 mode=manual tach=750 rpm=900' ;;
  *" power startup get "*) echo restore ;;
esac
EOF
cat > "$FAND" <<'EOF'
#!/bin/sh
state=''
while [ "$#" -gt 0 ]; do
  case "$1" in
    --state) state="$2"; shift 2 ;;
    *) shift ;;
  esac
done
cat > "$state" <<STATE
timestamp=123
model=dxp4800s
profile=custom
status=running
cpu_celsius=52
hdd_celsius=40
ssd_celsius=45
desired_pwm=64
applied_pwm=64
detail=
STATE
sleep 30
EOF
chmod +x "$BIN" "$FAND"
: > "$IT87_MODULE"

export TRIM_APPDEST="$APP_PROJECT/app" TRIM_PKGVAR="$TMP/var" UGREEN_RUNTIME_DIR="$TMP/run"
export UGREEN_PRODUCT_NAME="DXP4800S" BIOS_UGREENCTL="$BIN" BIOS_UGREENCTL_FAND="$FAND" BIOS_UGREENCTL_PLUGIN_DIR="$PLUGINS" BIOS_TEST_IT87_MODULE_PATH="$IT87_MODULE" UGREENCTL_CALLS="$CALLS"
request() {
  local path="$1" query="${2:-}" method="${3:-GET}"
  PATH_INFO="$path" QUERY_STRING="$query" REQUEST_METHOD="$method" CONTENT_LENGTH=0 bash "$API" | tail -n 1
}

CONFIG="$TMP/var/fan-curve.conf"
export UGREEN_PRODUCT_NAME="DXP6800"
json=$(request /bios/fan-curve 'action=start&mode=stock-6800pro&confirm=firmware-reversed' POST)
assert_contains "$json" '"ok":false'
json=$(request /bios/fan-curve 'action=start&mode=stock-6800&confirm=firmware-reversed' POST)
assert_contains "$json" '"ok":true'
assert_contains "$json" '"available":true,"profile":"stock-6800"'
assert_contains "$json" '"cpu":"25,38,55,75,90"'
assert_contains "$json" '"hdd":"30,35,43,48,55"'
assert_contains "$json" '"ssd":"45,50,60,65,70"'
assert_contains "$json" '"system_pwm":"64,130,210,230"'
assert_contains "$json" '"cpu_pwm":"80,130,210,230"'
FAND_PID=$(cat "$TMP/run/fan-curve.pid")
[[ "$(sed -n 's/^profile=//p' "$CONFIG")" == "stock-6800" ]] || fail "DXP6800 Pro-parameter compatibility preset was not persisted"
json=$(request /bios/fan-curve 'action=stop' POST)
assert_contains "$json" '"ok":true'
echo 'DXP6800 Pro-compatible curve API: start, values, persistence, mismatch rejection and stop passed'
