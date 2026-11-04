"""Compare unchanged official and personal weights on saved standing previews only."""
from pathlib import Path
import hashlib,importlib.util,json,struct
import cv2,numpy as np,tensorflow as tf

R=Path(__file__).resolve().parents[1];folder=R/'validation/standing_area_01'
spec=importlib.util.spec_from_file_location('replay','D:/fpga/Demo/V6_person_sensors/tests/test_video.py')
replay=importlib.util.module_from_spec(spec);spec.loader.exec_module(replay)
a=replay.Replay.__new__(replay.Replay);a.code=Path('D:/fpga/outputs/official_audit_20261023/replay_full.bin').read_bytes();a.size=96
models={'personal':R/'software/src/model/person_demo.tflite','official':Path('D:/fpga/Demo/efinix-tinyml-yolo-official/tinyml_generator/model/yolo_person_detect.tflite')}
report=dict(threshold=.4,nms=.30,results={},note='Saved RGB565 previews resized with AREA. Not exact neural input or same-frame board equivalence; no accuracy or independent ground truth. Person full: indices0..13, absent:16..26; partial excluded from summary.')
panels=[];gray=np.zeros((96,96),np.uint8)
for name,path in models.items():
 e=tf.lite.Interpreter(model_path=str(path),num_threads=1,experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF);e.allocate_tensors()
 inp=e.get_input_details()[0];heads=sorted(e.get_output_details(),key=lambda d:int(d['shape'][1]));assert inp['shape'].tolist()==[1,96,96,3]
 assert inp['dtype']==np.int8 and inp['quantization'][1]==-128 and np.isclose(inp['quantization'][0],1/255)
 rows=[]
 for i in range(31):
  image=cv2.imread(str(folder/f'frame_{i:02}.bmp'));assert image.shape==(180,320,3)
  rgb=cv2.resize(cv2.cvtColor(image,cv2.COLOR_BGR2RGB),(96,96),interpolation=cv2.INTER_AREA)
  e.set_tensor(inp['index'],(rgb.astype(np.int16)-128).astype(np.int8)[None]);e.invoke()
  outputs=[(e.get_tensor(d['index']).astype(np.float32)-d['quantization'][1])*d['quantization'][0]for d in heads]
  a.reset();a.u.mem_write(0x1000000c,struct.pack('<f',.4));boxes=a.detect(outputs,0,gray,gray)[:,:5]
  rows.append(dict(index=i,boxes=boxes.tolist()))
  if i in [0,3,5,9,10,13,17,20]:
   for box in boxes:
    xy=np.rint(box[:4]*[320,180,320,180]).astype(int);cv2.rectangle(image,tuple(xy[:2]),tuple(xy[2:]),(0,255,0),1)
   cv2.putText(image,f'{name} {i}: {len(boxes)}',(3,16),0,.45,(0,230,255),1);panels.append(image)
 result=dict(model_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),rows=rows,
  full_person_previews_with_box=sum(bool(r['boxes'])for r in rows[:14]),full_person_previews=14,
  empty_previews_with_box=sum(bool(r['boxes'])for r in rows[16:27]),empty_previews=11)
 report['results'][name]=result;print(name,json.dumps({k:v for k,v in result.items()if k!='rows'}),flush=True)
(folder/'model-comparison.json').write_text(json.dumps(report,indent=2))
assert len(panels)==16
cv2.imwrite(str(folder/'model-comparison.jpg'),np.vstack([np.hstack(panels[i:i+4])for i in range(0,16,4)]))
