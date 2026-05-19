# LongMemEval — 4f83e5d

**Dataset**: `longmemeval_oracle`  **Judge**: `codex exec (LongMemEval exact prompts)`  **Date**: 2026-05-19 06:39 UTC  **n**: 3

| model | overall | p50_ms | p95_ms |
|---|---|---|---|
| cc-soul/4f83e5d | 0.667 | 753 | 1244 |

## By question type

| type | accuracy | n |
|---|---|---|
| temporal-reasoning | 66.7% | 3 |

## Per-question

| qid | type | ✓ | ms | hypothesis | gold |
|---|---|---|---|---|---|
| gpt4_2655b836 | temporal-reasoning | ✓ | 461 | [6%] [result] [user] I've been doing some research and found a local detailer wi | GPS system not functioning correctly |
| gpt4_2487a7cb | temporal-reasoning | ✗ | 1244 | [10%] [episode] [assistant] Data visualization is a crucial step in data analysi | 'Data Analysis using Python' webinar |
| gpt4_76048e76 | temporal-reasoning | ✓ | 753 | [9%] [result] [user] By the way, speaking of bikes, I'm glad I got my bike repai | bike |
