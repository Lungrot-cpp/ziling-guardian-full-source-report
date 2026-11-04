"""Refresh the startup reference for the packaged model and unchanged fixed INT8 input."""
from pathlib import Path
import hashlib,json,re
import numpy as np
import tensorflow as tf

ROOT=Path(__file__).resolve().parents[1]
model=ROOT/'software/src/model'
binary=model/'person_demo.tflite'
assert binary.stat().st_size==227600
assert hashlib.sha256(binary.read_bytes()).hexdigest()=='0d74e3ef5c32dbc08fd3ae06eb705afb58d9bdd89562370c83de91d1d8e831ab'
source=(model/'input_img_000000018380_96x96x3.cc').read_text()
body=source[source.index('{')+1:source.index('}')]
values=[int(v,0) for v in re.findall(r'(?<!\w)-?0x[\da-fA-F]+|(?<!\w)-?\d+',body)]
image=np.asarray(values,dtype=np.int16).astype(np.int8).reshape(1,96,96,3)
runtime=tf.lite.Interpreter(model_content=binary.read_bytes(),experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
runtime.allocate_tensors();inp=runtime.get_input_details()[0]
assert inp['dtype']==np.int8 and np.isclose(inp['quantization'][0],1/255) and inp['quantization'][1]==-128
runtime.set_tensor(inp['index'],image);runtime.invoke()
lines=['#pragma once','#include <stdint.h>'];outputs={}
for d in runtime.get_output_details():
    grid=int(d['shape'][1]);assert grid in (3,6)
    name='fine' if grid==6 else 'coarse';v=runtime.get_tensor(d['index']).flatten()
    lines.append('static const int8_t person_reference_'+name+'[]={'+','.join(map(str,v.tolist()))+'};')
    outputs[name]=dict(bytes=len(v),sha256=hashlib.sha256(v.tobytes()).hexdigest(),scale=d['quantization'][0],zero_point=d['quantization'][1])
(model/'person_reference.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
report=dict(model_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),input_sha256=hashlib.sha256(image.tobytes()).hexdigest(),
            outputs=outputs,board_agreement_verified=False)
(ROOT/'validation/model_reference.json').write_text(json.dumps(report,indent=2))
print('MODEL_REFERENCE_PASS; new model reference generated; physical accelerator agreement not yet verified')
