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
        """Wrapper using the decoder stack directly to avoid positional-arg ambiguity."""
        def __init__(self, model):
            super().__init__()
            self.embed_tokens = model.model.embed_tokens
            self.decoder      = model.model   # QwenModel (decoder stack)
            self.lm_head      = model.lm_head
            self._vocab_size  = model.config.vocab_size

        def embed(self, input_ids: torch.Tensor) -> torch.Tensor:
            return self.embed_tokens(input_ids)

        def forward_from_embeds(self, embeds: torch.Tensor) -> torch.Tensor:
            # use_cache=False avoids KV cache shape being baked into the trace
            out = self.decoder(inputs_embeds=embeds, use_cache=False)
            return self.lm_head(out.last_hidden_state)

        def vocab_size(self) -> int:
            return self._vocab_size

    # Disable Flash Attention — tracing requires standard eager attention
    base.config._attn_implementation = "eager"
    # Re-init attention with eager implementation
    for layer in base.model.layers:
        if hasattr(layer.self_attn, '_attn_implementation'):
            layer.self_attn._attn_implementation = "eager"

    wrapper = QwenGradMemWrapper(base)
    wrapper.eval()

    # Sequence length must match production use — attention mask is baked into trace.
    # gradmemd pads/truncates all inputs to exactly SEQ_LEN tokens.
    SEQ_LEN = 512

    print(f"Tracing wrapper via trace_module (seq_len={SEQ_LEN}) ...")
    example_ids   = torch.zeros((1, SEQ_LEN), dtype=torch.long)
    example_embed = torch.zeros((1, SEQ_LEN, base.config.hidden_size))

    with torch.no_grad():
        scripted = torch.jit.trace_module(
            wrapper,
            inputs={
                "embed":              (example_ids,),
                "forward_from_embeds": (example_embed,),
            },
            strict=False,
        )

    # Save vocab_size alongside the model as a sidecar JSON
    import json, os
    meta = {"vocab_size": base.config.vocab_size, "hidden_size": base.config.hidden_size}
    meta_path = output_path.replace(".pt", ".json")
    with open(meta_path, "w") as f:
        json.dump(meta, f)
    print(f"Saved metadata → {meta_path}: {meta}")

    print(f"Saving to {output_path} ...")
    scripted.save(output_path)
    print(f"Model size: {os.path.getsize(output_path) / 1e9:.2f} GB")

    # Quick smoke test — must use same SEQ_LEN as tracing
    print("Smoke test ...")
    loaded = torch.jit.load(output_path)
    loaded.eval()
    test_ids = torch.zeros((1, SEQ_LEN), dtype=torch.long)
    with torch.no_grad():
        embeds = loaded.embed(test_ids)
        logits = loaded.forward_from_embeds(embeds)
    print(f"  embed:   {tuple(embeds.shape)}")
    print(f"  logits:  {tuple(logits.shape)}")
    assert embeds.shape[0] == 1
    assert logits.shape[-1] == meta["vocab_size"]
    print(f"OK — {output_path}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    export(sys.argv[1], sys.argv[2])
