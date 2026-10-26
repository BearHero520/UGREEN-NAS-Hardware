@echo off
if not defined UGREENCTL_ZIG (
  echo Set UGREENCTL_ZIG to the full path of an external zig.exe. 1>&2
  exit /b 2
)
"%UGREENCTL_ZIG%" cc -target aarch64-linux-gnu %*
