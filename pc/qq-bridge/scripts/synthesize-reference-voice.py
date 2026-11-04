import json
import sys
from pathlib import Path

import soundfile as sf
import torch
from qwen_tts import Qwen3TTSModel

request = json.loads(sys.stdin.buffer.read().decode("utf-8"))
text = request["text"]
if not isinstance(text, str) or not text.strip() or len(text) > 300:
    raise ValueError("Speech text must contain 1–300 characters")

root = Path(__file__).resolve().parent.parent
voices = {
    "yujie": ("voice-yujie.wav", "别着急，我在这里。今天累了的话，就先歇一会儿。有话可以慢慢说，我会认真听。等你准备好了，我们再聊聊今天发生的事。"),
    "original": ("voice-reference.wav", None),
}
reference, transcript = voices[request.get("voiceId", "original")]
torch.set_num_threads(4)
model = Qwen3TTSModel.from_pretrained(
    str(root / "models/qwen3-tts-0.6b"),
    device_map="cuda:0",
    dtype=torch.bfloat16,
    attn_implementation="sdpa",
)
# The singing reference uses only its speaker embedding; the designed speaking voice also has a transcript.
wavs, sample_rate = model.generate_voice_clone(
    text=text.strip(),
    language="Chinese",
    ref_audio=str(root / "assets" / reference),
    ref_text=transcript,
    x_vector_only_mode=transcript is None,
    non_streaming_mode=True,
    max_new_tokens=2048,
)
sf.write(request["path"], wavs[0], sample_rate, subtype="PCM_16")
