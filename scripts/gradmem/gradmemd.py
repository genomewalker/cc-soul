#!/usr/bin/env python3
"""
gradmemd.py — GradMem write subprocess.

Reads a JSON job from stdin, runs K gradient descent steps over memory
prefix tokens M using a frozen Qwen2.5-1.5B transformer, writes result JSON
to stdout.

Called by gradmemd.cpp (C++ launcher) which handles the daemon integration,
subprocess lifecycle, and result persistence.

Input JSON:
  {model_path, text, session_id, realm, n_mem_tokens, K, inner_lr, max_ctx_tokens}

Output JSON:
  {session_id, realm, M_fp16_b64, write_loss, n_mem_tokens, d_model, K, inner_lr, proxy_embedding}
"""

import sys
import json
import base64
import struct

import torch
from transformers import AutoModelForCausalLM, AutoTokenizer


def run(job: dict) -> dict:
    model_path    = job["model_path"]
    text          = job["text"]
    session_id    = job["session_id"]
    realm         = job.get("realm", "brahman")
    n_mem_tokens  = job.get("n_mem_tokens", 8)
    K             = job.get("K", 2)
    inner_lr      = job.get("inner_lr", 0.04)
    max_ctx       = job.get("max_ctx_tokens", 512)

    # ── Load model (frozen) ────────────────────────────────────────────────────
    model = AutoModelForCausalLM.from_pretrained(
        model_path,
        dtype=torch.float32,
        device_map="cpu",
        trust_remote_code=True,
    )
    model.eval()
    for p in model.parameters():
        p.requires_grad_(False)

    tokenizer = AutoTokenizer.from_pretrained(model_path, trust_remote_code=True)
    d_model   = model.config.hidden_size
    vocab_size = model.config.vocab_size

    # ── Tokenize and pad to max_ctx ────────────────────────────────────────────
    ids = tokenizer.encode(text, add_special_tokens=True)
    if len(ids) > max_ctx:
        ids = ids[:max_ctx]
    else:
        ids = ids + [tokenizer.eos_token_id or 0] * (max_ctx - len(ids))

    input_ids = torch.tensor([ids], dtype=torch.long)  # [1, max_ctx]

    # ── Context embeddings (no grad) ──────────────────────────────────────────
    with torch.no_grad():
        ctx_embeds = model.model.embed_tokens(input_ids)  # [1, max_ctx, d]

    # ── Initialize M (learnable leaf) ─────────────────────────────────────────
    M = (torch.randn(1, n_mem_tokens, d_model) * 0.02).requires_grad_(True)

    # ── Inner loop: K SGD steps ───────────────────────────────────────────────
    last_loss = 0.0
    for _ in range(K):
        if M.grad is not None:
            M.grad.zero_()

        # Prepend M to context embeddings: [1, n_mem+max_ctx, d]
        x_write = torch.cat([M, ctx_embeds], dim=1)

        # Forward through decoder stack (no KV cache, no grad on model weights)
        with torch.no_grad():
            # Get decoder output — we need to call with inputs_embeds and
            # allow grad flow only through M by using a custom forward.
            pass

        # Re-do forward allowing grad through M only
        out = model.model(inputs_embeds=x_write, use_cache=False)
        logits = model.lm_head(out.last_hidden_state)  # [1, n_mem+max_ctx, vocab]

        # Reconstruction: predict input_ids from the n_mem-offset positions
        target_logits = logits[:, n_mem_tokens:-1, :]   # [1, max_ctx-1, vocab]
        targets       = input_ids[:, 1:]                 # [1, max_ctx-1]

        loss = torch.nn.functional.cross_entropy(
            target_logits.reshape(-1, vocab_size),
            targets.reshape(-1),
        )
        last_loss = loss.item()

        loss.backward()

        with torch.no_grad():
            M -= inner_lr * M.grad  # SGD step

    # ── Serialize M → fp16 → base64 ──────────────────────────────────────────
    M_np = M.squeeze(0).detach().to(torch.float16).contiguous()
    M_bytes = M_np.numpy().tobytes()
    M_b64   = base64.b64encode(M_bytes).decode("ascii")

    # ── Proxy embedding: mean(M) ──────────────────────────────────────────────
    proxy = M.squeeze(0).detach().to(torch.float32).mean(dim=0).tolist()

    return {
        "session_id":     session_id,
        "realm":          realm,
        "M_fp16_b64":     M_b64,
        "write_loss":     last_loss,
        "n_mem_tokens":   n_mem_tokens,
        "d_model":        d_model,
        "K":              K,
        "inner_lr":       inner_lr,
        "proxy_embedding": proxy,
    }


if __name__ == "__main__":
    raw = sys.stdin.read().strip()
    if not raw:
        print(json.dumps({"error": "empty stdin"}))
        sys.exit(1)

    try:
        job = json.loads(raw)
    except Exception as e:
        print(json.dumps({"error": f"parse: {e}"}))
        sys.exit(1)

    try:
        result = run(job)
        print(json.dumps(result))
    except Exception as e:
        import traceback
        print(json.dumps({"error": str(e), "traceback": traceback.format_exc()}))
        sys.exit(1)
