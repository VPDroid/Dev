#!/usr/bin/env bash

known_tests=(
  net_test_btcore
  net_test_device
  net_test_hci
  net_test_osi
)

usage() {
  echo "Usage: $0 [--all|--help|<test names>]"
  echo ""
  echo "Known test names:"

  for name in ${known_tests[*]}
  do
    echo "    $name"
  done
}

run_tests() {
  for name in $*
  do
    echo "--- $name ---"
    echo "pushing..."
    adb push {$ANDROID_PRODUCT_OUT,}/data/nativetest/$name/$name
    echo "running..."
    adb shell data/nativetest/$name/$name
  done
}

if [ $# -eq 0 ] || [ $1 == "--help" ]; then
  usage
elif [ $1 == "--all" ]; then
  run_tests ${known_tests[*]}
else
  run_tests $*
fi

