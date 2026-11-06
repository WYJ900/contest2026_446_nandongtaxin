set -e
S=$HOME/velaride_agent_emu/cmake_out/vela_goldfish-arm64-v8a-ap
D=/mnt/d/desktop/VelaRide_AI/cmake_out/vela_goldfish-arm64-v8a-ap
mkdir -p $D/system
cp $S/.config $S/nuttx $S/vela_ap.bin $S/vela_ap.elf $S/vela_data.bin $S/vela_system.bin $S/nuttx.manifest $D/
echo "ro.product.cpu.abi=arm64" > $D/system/build.prop
ls -la $D | head -20
