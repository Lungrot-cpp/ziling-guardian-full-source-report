"""Compare resizing before decimation on the existing development images."""
from pathlib import Path
import hashlib,importlib.util,json,struct,sys,time
import cv2
import numpy as np
import tensorflow as tf

R=Path(__file__).resolve().parents[1]
B=Path('D:/fpga/outputs/personal_finetune_20261024/retention/calibration67')
sys.path.insert(0,str(B/'source'));from metrics import score
model=R/'software/src/model/person_demo.tflite'
meta=json.loads((B/'int8_validation.json').read_text());assert hashlib.sha256(model.read_bytes()).hexdigest()==meta['sha256']
spec=importlib.util.spec_from_file_location('replay','D:/fpga/Demo/V6_person_sensors/tests/test_video.py')
replay=importlib.util.module_from_spec(spec);spec.loader.exec_module(replay)
a=replay.Replay.__new__(replay.Replay);a.code=Path('D:/fpga/outputs/official_audit_20261023/replay_full.bin').read_bytes();a.size=96
e=tf.lite.Interpreter(model_path=str(model),num_threads=1,experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
e.allocate_tensors();inp=e.get_input_details()[0];heads=sorted(e.get_output_details(),key=lambda d:int(d['shape'][1]));gray=np.zeros((96,96),np.uint8)
images=np.load(B/'export/validation_inputs.npy');labels=json.loads((B/'export/validation_labels.json').read_text());cached=[(None,r['boxes'])for r in labels]
original=json.loads((B/'int8_validation_boxes.json').read_text())['0.4'];predictions=[]
assert len(images)==len(labels)==len(original)==600
start=time.monotonic()
for i,(row,old) in enumerate(zip(labels,images)):
 image=cv2.imread(row['image']);assert image is not None
 h,w=image.shape[:2];ch,cw=max(h,round(w*9/16)),max(w,round(h*16/9));y,x=(ch-h)//2,(cw-w)//2
 scene=np.zeros((ch,cw,3),np.uint8);scene[y:y+h,x:x+w]=cv2.cvtColor(image,cv2.COLOR_BGR2RGB)
 assert np.array_equal(cv2.resize(scene,(96,96),interpolation=cv2.INTER_NEAREST),old),i
 rgb=cv2.resize(scene,(96,96),interpolation=cv2.INTER_AREA)
 e.set_tensor(inp['index'],(rgb.astype(np.int16)-128).astype(np.int8)[None]);e.invoke()
 outputs=[(e.get_tensor(d['index']).astype(np.float32)-d['quantization'][1])*d['quantization'][0]for d in heads]
 a.reset();a.u.mem_write(0x1000000c,struct.pack('<f',.40));predictions.append(a.detect(outputs,0,gray,gray)[:,:5].tolist())
 if i%100==99:print('images',i+1,flush=True)
result=dict(images=600,threshold=.40,nms=.30,elapsed_seconds=time.monotonic()-start,
 before=score([np.asarray(b).reshape(-1,5)for b in original],cached,.40),
 after=score([np.asarray(b).reshape(-1,5)for b in predictions],cached,.40),
 note='Existing development data, not independent accuracy. Area resize of original padded source versus original nearest. Not a simulation of sensor optics or Bayer processing.')
result['per_source']={}
for source in sorted(set(r['source']for r in labels)):
 ids=[i for i,r in enumerate(labels)if r['source']==source]
 result['per_source'][source]=dict(images=len(ids),before=score([np.asarray(original[i]).reshape(-1,5)for i in ids],[cached[i]for i in ids],.40),after=score([np.asarray(predictions[i]).reshape(-1,5)for i in ids],[cached[i]for i in ids],.40))
result['passed']=all(result['after'][k]>=result['before'][k]for k in ['precision','recall','f1'])
(R/'validation/area-validation.json').write_text(json.dumps(result,indent=2))
(R/'validation/area-validation-boxes.json').write_text(json.dumps(predictions))
print(json.dumps(result),flush=True)
