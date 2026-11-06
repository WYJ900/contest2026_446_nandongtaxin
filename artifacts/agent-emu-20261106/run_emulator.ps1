# Launch the goldfish-arm64 ai_agent build with the official Windows emulator (same args as emulator.sh).
$Top = 'D:\desktop\VelaRide_AI'
$Out = Join-Path $Top 'cmake_out\vela_goldfish-arm64-v8a-ap'
$env:ANDROID_EMULATOR_VELA = 'true'
$env:ANDROID_BUILD_TOP = $Top
$env:ANDROID_PRODUCT_OUT = $Out
& (Join-Path $Top 'prebuilts\emulator\windows-x86_64\emulator.exe') -show-kernel -verbose -no-snapshot `
  -qemu -device 'virtio-snd,bus=virtio-mmio-bus.2' -allow-host-audio
