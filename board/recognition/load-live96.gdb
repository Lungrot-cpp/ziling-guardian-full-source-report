set confirm off
set pagination off
set arch riscv:rv32
set remotetimeout 250
set mem inaccessible-by-default off
file "D:/fpga/V12_FPS_Voice_J28_20261031/recognition/software/build-accel/personInferenceDemo.elf"
target extended-remote :4333
monitor reset init
monitor halt
monitor fpga_spinal.cpu0 configure -event gdb-detach { halt }
if *(unsigned int*)0xf8100000 != 0x56364631
echo INTERFACE_CHECK_FAILED: V6F1\n
quit 1
end
if *(unsigned int*)0xf8100320 != 0x53454e31
echo INTERFACE_CHECK_FAILED: SEN1\n
quit 1
end
load
set $pc = 0x1000
monitor fpga_spinal.cpu0 configure -event gdb-detach {}
monitor resume 0x1000
disconnect
quit
