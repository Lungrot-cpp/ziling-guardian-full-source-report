.section .rodata
.balign 16
.global live192_model
live192_model:
.incbin "src/model/yolo192.tflite"
.balign 16
.global live192_inputs
live192_inputs:
.incbin "src/model/fixed192_inputs.bin"
.balign 16
.global live192_reference
live192_reference:
.incbin "src/model/fixed192_reference.bin"
