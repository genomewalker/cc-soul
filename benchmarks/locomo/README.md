# LoCoMo Benchmark for cc-soul

Evaluate cc-soul's long-term conversational memory against the [LoCoMo benchmark](https://github.com/snap-research/locomo) (ACL 2024).

## Setup

```bash
# Clone LoCoMo dataset
git clone https://github.com/snap-research/locomo /tmp/locomo

# Install dependencies
pip install -r requirements.txt
python -c "import nltk; nltk.download('punkt')"

# Ensure ANTHROPIC_API_KEY is set
export ANTHROPIC_API_KEY=your_key_here
```

## Run Benchmark

```bash
# Full benchmark (all 10 conversations)
python evaluate.py --data-file /tmp/locomo/data/locomo10.json

# Single conversation (for testing)
python evaluate.py --data-file /tmp/locomo/data/locomo10.json --samples sample_0

# With custom model
python evaluate.py --data-file /tmp/locomo/data/locomo10.json --model claude-3-opus-20240229
```

## Evaluation Process

1. **Ingest**: Each conversation is loaded into cc-soul as memories
   - Sessions become observations with dialog IDs
   - Session summaries become insights
   - Each conversation gets its own realm for isolation

2. **Recall**: For each QA pair, `full_resonate` retrieves relevant context

3. **Answer**: An LLM answers based on retrieved context

4. **Score**: F1 score calculated against ground truth

## QA Categories

| Category | Type | Scoring |
|----------|------|---------|
| 1 | Multi-hop | Partial F1 for sub-answers |
| 2 | Single-hop | Token overlap F1 |
| 3 | Temporal | F1 on first answer |
| 4 | Open-domain | Token overlap F1 |
| 5 | Adversarial | Binary (detect "no info") |

## Comparison Baselines

| System | Overall F1 |
|--------|-----------|
| Human ceiling | 87.9% |
| AutoMem | 90.5% |
| GPT-4 | 32.1% |
| cc-soul | TBD |

## Output

Results saved to `locomo_results.json`:

```json
{
  "overall_f1": 0.xxx,
  "total_qa": 1990,
  "categories": {
    "1": {"name": "Multi-hop", "count": N, "f1": 0.xxx},
    ...
  },
  "results": [...]
}
```
