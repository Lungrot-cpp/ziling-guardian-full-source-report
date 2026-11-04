// OP_ERR selects the existing reference kernels. OP_BYPASS would skip computation.
#include "platform/tinyml/ops/add.h"
OP_STATUS_T add_drv(
		int size,
		const int8_t *input1_data,
		const int8_t *input2_data,
		int8_t *output_data,
		int32_t input1_offset,
		int32_t input2_offset,
		int32_t output_offset,
		int32_t output_multiplier,
		int32_t output_shift,
		int32_t left_shift,
		int32_t input1_multiplier,
		int32_t input1_shift,
		int32_t input2_multiplier,
		int32_t input2_shift,
		int32_t quantized_activation_min,
		int32_t quantized_activation_max
		) { return OP_ERR; }
#include "platform/tinyml/ops/conv.h"
OP_STATUS_T conv_drv(
		const int stride_width,
		const int stride_height,
		const int dilation_width_factor,
		const int dilation_height_factor,
		const int pad_width,
		const int pad_height,
		const int32_t input_offset,
		const int32_t output_offset,

		const int batches,
		const int input_depth,
		const int output_depth,
		const int input_width,
		const int input_height,
		const int filter_width,
		const int filter_height,
		const int output_width,
		const int output_height,
		const int32_t output_activation_min,
		const int32_t output_activation_max,

		const int32_t *output_multiplier,
		const int32_t *output_shift,

		const int8_t *input_data,
		const int8_t *filter_data,
		const int32_t *bias_data,
		const int32_t *input_shape,
		const int32_t *output_shape,
		const int32_t *filter_shape,

		int8_t *output_data
		) { return OP_ERR; }
#include "platform/tinyml/ops/depthwise.h"
OP_STATUS_T depthwise_drv(
		const int stride_width,
		const int stride_height,
		const int dilation_width_factor,
		const int dilation_height_factor,
		const int pad_width,
		const int pad_height,
		const int depth_multiplier,
		const int32_t input_offset,
		const int32_t output_offset,
		const int32_t output_activation_min,
		const int32_t output_activation_max,

		const int batches,
		const int input_depth,
		const int output_depth,
		const int input_width,
		const int input_height,
		const int filter_width,
		const int filter_height,
		const int output_width,
		const int output_height,

		const int32_t* output_multiplier,
		const int32_t* output_shift,
		const int8_t* input_data,
		const int8_t* filter_data,
		const int32_t* bias_data,
		const int32_t *input_shape,
		const int32_t *output_shape,
		const int32_t *filter_shape,

		int8_t* output_data
		) { return OP_ERR; }
#include "platform/tinyml/ops/fully_connected.h"
OP_STATUS_T fully_connected_drv(

    const int32_t input_offset,
    const int32_t filter_offset,
    const int32_t output_offset,
    const int32_t output_multiplier,
    const int output_shift,
    const int32_t output_activation_min,
    const int32_t output_activation_max,

    const int batches,
    const int output_depth,
    const int accum_depth,

    const int8_t *input_data,
    const int8_t *filter_data,
    const int32_t *bias_data,
    int32_t *acc_store
) { return OP_ERR; }
#include "platform/tinyml/ops/lr.h"
OP_STATUS_T lr_drv(
		int size,
		int32_t input_offset,

		int32_t output_shift_identity,
		int32_t output_multiplier_identity,
		int32_t output_shift_alpha,
		int32_t output_multiplier_alpha,
		int32_t output_offset,

		int32_t quantized_activation_max,
		int32_t quantized_activation_min,
		const int8_t *input_data,
		int8_t *output_data
		) { return OP_ERR; }
#include "platform/tinyml/ops/mul.h"
OP_STATUS_T mul_drv(
		int32_t input1_offset,
		int32_t input2_offset,

		int32_t output_shift,
		int32_t output_multiplier,
		int32_t output_offset,

		int32_t quantized_activation_max,
		int32_t quantized_activation_min,
		int32_t bmax,
		int32_t ymax,
		int32_t xmax,
		int32_t cmax,
		const int *strides1,
		const int *strides2,
		const int8_t *input1_data,
		const int8_t *input2_data,
		int8_t *output_data
		) { return OP_ERR; }
OP_STATUS_T mul_drv_elementwise(
		int size,
		int32_t input1_offset,
		int32_t input2_offset,

		int32_t output_shift,
		int32_t output_multiplier,
		int32_t output_offset,

		int32_t quantized_activation_max,
		int32_t quantized_activation_min,
		const int8_t *input1_data,
		const int8_t *input2_data,
		int8_t *output_data
		) { return OP_ERR; }
