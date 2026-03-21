#!/usr/bin/env python3
"""
Export Qwen2.5-1.5B-Instruct to TorchScript for gradmemd.

Creates qwen_gradmem.pt with three methods:
  - embed(input_ids)           -> embeddings [B, seq, d]
  - forward_from_embeds(embeds) -> logits    [B, seq, vocab]
  - vocab_size()               -> int

Usage:
    python export_model.py <hf_model_path> <output_path>

Example:
    python export_model.py \
      ~/.cache/huggingface/hub/models--Qwen--Qwen2.5-1.5B-Instruct/snapshots/989aa7... \
      ~/.claude/bin/qwen_gradmem.pt
"""

import sys
import torch
from transformers import AutoModelForCausalLM


def export(hf_model_path: str, output_path: str) -> None:
    print(f"Loading {hf_model_path} ...")
    base = AutoModelForCausalLM.from_pretrained(
        hf_model_path,
        torch_dtype=torch.float32,
        device_map="cpu",
        trust_remote_code=True,
    )
    base.eval()

    class QwenGradMemWrapper(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.embed_tokens = model.model.embed_tokens
            self.layers       = model.model.layers
            self.norm         = model.model.norm
            self.lm_head      = model.lm_head
            self._vocab_size  = model.config.vocab_size

        def embed(self, input_ids: torch.Tensor) -> torch.Tensor:
            return self.embed_tokens(input_ids)

        def forward_from_embeds(self, embeds: torch.Tensor) -> torch.Tensor:
            hidden = embeds
            seq_len = hidden.size(1)
            position_ids = torch.arange(seq_len, device=hidden.device).unsqueeze(0)
            for layer in self.layers:
                # Most Qwen layers accept (hidden_states, attention_mask, position_ids)
                # passing None for attention_mask triggers causal masking internally.
                layer_out = layer(hidden, attention_mask=None, position_ids=position_ids)
                hidden = layer_out[0]
            hidden = self.norm(hidden)
            return self.lm_head(hidden)

        def vocab_size(self) -> int:
            return self._vocab_size

    wrapper = QwenGradMemWrapper(base)
    wrapper.eval()

    print("Scripting wrapper ...")
    scripted = torch.jit.script(wrapper)

    print(f"Saving to {output_path} ...")
    scripted.save(output_path)

    # Quick smoke test
    print("Smoke test ...")
    loaded = torch.jit.load(output_path)
    loaded.eval()
    test_ids = torch.tensor([[1, 100, 200, 300]])
    with torch.no_grad():
        embeds = loaded.embed(test_ids)
        logits = loaded.forward_from_embeds(embeds)
        vsize  = loaded.vocab_size()
    print(f"  embed:   {tuple(embeds.shape)}")
    print(f"  logits:  {tuple(logits.shape)}")
    print(f"  vocab:   {vsize}")
    assert embeds.shape[0] == 1
    assert logits.shape[-1] == vsize
    print(f"OK — {output_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    export(sys.argv[1], sys.argv[2])
