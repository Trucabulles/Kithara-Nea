from pathlib import Path
import sys,json,hashlib
import numpy as np
import torch
import onnxruntime as ort
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'src'))
from accords_local.beat_this_official import OfficialSmall0
class Outputs(torch.nn.Module):
    def __init__(self,net):super().__init__();self.net=net
    def forward(self,x):
        result=self.net(x);return result['beat'],result['downbeat']
model=Outputs(OfficialSmall0().net).eval();folder=ROOT/'resources/models/beat_this_official';target=folder/'small0.onnx'
x=torch.zeros((1,1500,128),dtype=torch.float32)
with torch.inference_mode():torch.onnx.export(model,x,str(target),input_names=['spectrogram'],output_names=['beat','downbeat'],opset_version=17,dynamo=False)
options=ort.SessionOptions();options.intra_op_num_threads=2;session=ort.InferenceSession(str(target),options,providers=['CPUExecutionProvider'])
errors=[]
for seed in range(3):
    generator=torch.Generator().manual_seed(seed);value=torch.rand((1,1500,128),generator=generator)*5
    with torch.inference_mode():expected=model(value)
    actual=session.run(None,{'spectrogram':value.numpy()})
    errors.append([float(np.max(np.abs(a-b.numpy()))) for a,b in zip(actual,expected)])
assert max(max(e) for e in errors)<.001,errors
(folder/'onnx-provenance.json').write_text(json.dumps({'sourceWeightsSha256':'6074be2c4d490c5f6101fcc374a1ec72ae93456e23bb6019783b849f5dc7d47b','sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'input':[1,1500,128],'opset':17,'maximumLogitError':errors,'runtime':'ONNX Runtime CPU'},indent=2),encoding='utf-8')
print('ONNX export verified',errors)
