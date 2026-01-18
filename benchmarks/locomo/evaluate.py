#!/usr/bin/env python3
"""
LoCoMo Benchmark Evaluation for cc-soul

Evaluates cc-soul's memory system against the LoCoMo benchmark for
long-term conversational memory.

Usage:
    python evaluate.py --data-file /path/to/locomo10.json --model claude-3-5-sonnet-20241022

Phases:
1. Ingest: Load conversations into cc-soul as memories
2. Recall: For each QA pair, use cc-soul to retrieve relevant context
3. Answer: Use LLM to answer based on retrieved context
4. Evaluate: Calculate F1 scores against ground truth
"""

import argparse
import json
import os
import re
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import regex
import unicodedata
from nltk.stem import PorterStemmer

ps = PorterStemmer()

# Chitta CLI path
CHITTA_BIN = os.path.expanduser("~/.claude/bin/chitta")
SOCKET_PATH = None  # Set via --socket arg


@dataclass
class QAResult:
    question: str
    ground_truth: str
    prediction: str
    f1: float
    category: int
    evidence: list


def call_chitta(tool: str, **params) -> dict:
    """Call chitta CLI tool and return JSON result."""
    global SOCKET_PATH
    cmd = [CHITTA_BIN, tool, "--json"]
    if SOCKET_PATH:
        cmd.extend(["--socket-path", SOCKET_PATH])
    for k, v in params.items():
        if v is not None:
            cmd.extend([f"--{k.replace('_', '-')}", str(v)])

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return {"error": result.stderr}

    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return {"text": result.stdout}


def normalize_answer(s: str) -> str:
    """Normalize answer for comparison."""
    s = s.replace(',', "")

    def remove_articles(text):
        return regex.sub(r'\b(a|an|the|and)\b', ' ', text)

    def white_space_fix(text):
        return ' '.join(text.split())

    def remove_punc(text):
        import string
        exclude = set(string.punctuation)
        return ''.join(ch for ch in text if ch not in exclude)

    def lower(text):
        return text.lower()

    return white_space_fix(remove_articles(remove_punc(lower(s))))


def f1_score(prediction: str, ground_truth: str) -> float:
    """Calculate F1 score between prediction and ground truth."""
    prediction_tokens = [ps.stem(w) for w in normalize_answer(prediction).split()]
    ground_truth_tokens = [ps.stem(w) for w in normalize_answer(ground_truth).split()]

    common = Counter(prediction_tokens) & Counter(ground_truth_tokens)
    num_same = sum(common.values())

    if num_same == 0:
        return 0.0

    precision = num_same / len(prediction_tokens) if prediction_tokens else 0
    recall = num_same / len(ground_truth_tokens) if ground_truth_tokens else 0

    if precision + recall == 0:
        return 0.0

    return (2 * precision * recall) / (precision + recall)


def f1_multi(prediction: str, ground_truth: str) -> float:
    """F1 for multi-answer questions (comma-separated)."""
    predictions = [p.strip() for p in prediction.split(',')]
    ground_truths = [g.strip() for g in ground_truth.split(',')]

    import numpy as np
    return np.mean([max([f1_score(pred, gt) for pred in predictions]) for gt in ground_truths])


def extract_entities_and_triplets(text: str, speaker: str, timestamp: str, dialog_id: str):
    """Extract entities and relationships from conversation turn."""
    triplets = []
    entities = set()

    # Add speaker as entity
    entities.add(speaker)

    # Simple patterns for entity extraction (could be enhanced with NLP)
    # Dates
    date_patterns = [
        r'\b(\d{1,2}\s+(?:January|February|March|April|May|June|July|August|September|October|November|December)\s+\d{4})\b',
        r'\b(\d{4})\b',  # Year
        r'\b((?:Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday))\b',
    ]

    for pattern in date_patterns:
        for match in re.findall(pattern, text, re.IGNORECASE):
            entities.add(match)
            triplets.append((dialog_id, "occurred_at", match))

    # Events (simple heuristics)
    event_keywords = ['went to', 'visited', 'attended', 'started', 'finished', 'joined', 'left']
    for kw in event_keywords:
        if kw in text.lower():
            # Extract the phrase after the keyword
            idx = text.lower().find(kw)
            phrase = text[idx:idx+50].split('.')[0].split(',')[0]
            triplets.append((speaker, kw.replace(' ', '_'), phrase))

    return entities, triplets


def ingest_conversation(sample_id: str, conversation: dict, session_summaries: dict = None):
    """Ingest a conversation into cc-soul memory using SSL patterns and triplets."""
    print(f"  Ingesting conversation {sample_id}...")

    # Extract speakers
    speaker_a = conversation.get('speaker_a', 'Speaker A')
    speaker_b = conversation.get('speaker_b', 'Speaker B')

    # Create speaker entities and their relationship
    all_triplets = [
        (speaker_a, "talks_with", speaker_b),
        (speaker_b, "talks_with", speaker_a),
        (sample_id, "contains", speaker_a),
        (sample_id, "contains", speaker_b),
    ]

    # Find all sessions
    session_keys = sorted([k for k in conversation.keys() if k.startswith('session_') and not k.endswith('_date_time')])
    num_sessions = len(session_keys)

    for i, session_key in enumerate(session_keys):
        timestamp_key = f"{session_key}_date_time"
        timestamp = conversation.get(timestamp_key, f'session_{i+1}')
        session_num = i + 1

        turns = conversation.get(session_key, [])
        if not turns:
            continue

        # Build session text with dialog IDs for reference
        turns_text = []
        for turn in turns:
            speaker = turn.get('speaker', 'Unknown')
            text = turn.get('text', '')
            dialog_id = turn.get('dia_id', '')
            turns_text.append(f"[{dialog_id}] {speaker}: {text}")

            # Extract entities and triplets from each turn
            entities, triplets = extract_entities_and_triplets(text, speaker, timestamp, dialog_id)
            all_triplets.extend(triplets)

            # Link dialog to speaker and session
            all_triplets.append((dialog_id, "spoken_by", speaker))
            all_triplets.append((dialog_id, "part_of", f"session_{session_num}"))

        session_text = '\n'.join(turns_text)

        # Store session as SSL pattern
        ssl_content = f"""[{sample_id}] Session {session_num}→{timestamp}→conversation
[ε] {speaker_a} and {speaker_b} conversation on {timestamp}. {len(turns)} turns.
{session_text}"""

        call_chitta(
            "observe",
            category="episode",
            title=f"Session {session_num} ({timestamp})",
            content=ssl_content,
            tags=f"locomo,{sample_id},session_{session_num}"
        )

        # Add session triplets
        all_triplets.append((f"session_{session_num}", "occurred_at", timestamp))
        all_triplets.append((sample_id, "contains", f"session_{session_num}"))

        # Store session summary as wisdom if available
        if session_summaries:
            summary_key = f"{session_key}_summary"
            summary = session_summaries.get(summary_key, "")
            if summary:
                call_chitta(
                    "grow",
                    type="wisdom",
                    title=f"Summary: Session {session_num}",
                    content=f"[{sample_id}] Session {session_num} summary\n[ε] {summary}",
                    tags=f"locomo,{sample_id},summary"
                )

    # Batch store all triplets
    if all_triplets:
        # Use connect for triplet storage
        for subj, pred, obj in all_triplets[:100]:  # Limit to avoid overwhelming
            call_chitta("connect", subject=subj, predicate=pred, object=obj, weight=0.8)

    print(f"  Ingested {num_sessions} sessions, {len(all_triplets)} triplets")


def recall_for_question(question: str, sample_id: str) -> str:
    """Use cc-soul recall with multi-hop triplet traversal."""
    context_parts = []

    # 1. Use full_resonate for semantic recall with tag filter
    result = call_chitta(
        "recall",
        query=question,
        zoom="sparse",
        limit=10,
        tag=sample_id
    )

    text = result.get("text", "")
    if text and "error" not in result:
        context_parts.append(text)

    # 2. Try multi-hop for entity connections
    # Extract key terms from question for entity lookup
    key_terms = [w for w in question.split() if len(w) > 3 and w[0].isupper()]
    for term in key_terms[:3]:  # Limit to avoid too many queries
        hop_result = call_chitta(
            "multi_hop",
            start_query=term,
            hops=2,
            limit=5
        )
        if "text" in hop_result and "error" not in hop_result:
            context_parts.append(hop_result["text"])

    # 3. Query triplets for structural context
    for term in key_terms[:2]:
        triplet_result = call_chitta(
            "query",
            subject=term,
            limit=10
        )
        if "text" in triplet_result and "error" not in triplet_result:
            context_parts.append(triplet_result["text"])

    return "\n\n---\n\n".join(context_parts[:5]) if context_parts else ""


def answer_with_llm(question: str, context: str, model: str) -> str:
    """Use LLM to answer question based on context."""
    if not context:
        return "No information available"

    prompt = f"""Based on the following conversation context, answer the question concisely.
If the answer cannot be determined from the context, say "No information available".

Context:
{context}

Question: {question}

Answer:"""

    # Use anthropic API if available
    try:
        import anthropic
        client = anthropic.Anthropic()

        response = client.messages.create(
            model=model,
            max_tokens=256,
            messages=[{"role": "user", "content": prompt}]
        )
        return response.content[0].text.strip()
    except ImportError:
        # Fallback: use chitta's built-in LLM if available
        return "LLM not available"
    except Exception as e:
        return f"Error: {e}"


def evaluate_qa(qa: dict, prediction: str, category: int) -> float:
    """Evaluate a single QA pair."""
    answer = str(qa.get('answer', ''))

    # Category 3: temporal - use first answer if semicolon-separated
    if category == 3:
        answer = answer.split(';')[0].strip()

    # Category 5: adversarial - check for "no information"
    if category == 5:
        if 'no information available' in prediction.lower() or 'not mentioned' in prediction.lower():
            return 1.0
        return 0.0

    # Category 1: multi-hop - use multi-answer F1
    if category == 1:
        return f1_multi(prediction, answer)

    # Categories 2, 3, 4: single answer F1
    return f1_score(prediction, answer)


def run_benchmark(data_file: str, model: str, output_file: str, sample_ids: list = None):
    """Run the full benchmark."""
    print(f"Loading data from {data_file}")
    with open(data_file) as f:
        samples = json.load(f)

    if sample_ids:
        samples = [s for s in samples if s['sample_id'] in sample_ids]

    all_results = []

    for sample in samples:
        sample_id = sample['sample_id']
        print(f"\n=== Processing sample {sample_id} ===")

        # Phase 1: Ingest conversation
        ingest_conversation(
            sample_id,
            sample['conversation'],
            sample.get('session_summary', [])
        )

        # Phase 2-4: Answer each QA pair
        qa_results = []
        for i, qa in enumerate(sample['qa']):
            question = qa['question']
            category = qa['category']

            # Recall context
            context = recall_for_question(question, sample_id)

            # Answer with LLM
            prediction = answer_with_llm(question, context, model)

            # Evaluate
            score = evaluate_qa(qa, prediction, category)

            result = QAResult(
                question=question,
                ground_truth=str(qa.get('answer', '')),
                prediction=prediction,
                f1=score,
                category=category,
                evidence=qa.get('evidence', [])
            )
            qa_results.append(result)

            if (i + 1) % 20 == 0:
                avg_f1 = sum(r.f1 for r in qa_results) / len(qa_results)
                print(f"  Progress: {i+1}/{len(sample['qa'])} QA pairs, avg F1: {avg_f1:.3f}")

        # Calculate sample statistics
        sample_f1 = sum(r.f1 for r in qa_results) / len(qa_results)
        print(f"  Sample {sample_id} F1: {sample_f1:.3f}")

        all_results.extend(qa_results)

    # Overall statistics
    overall_f1 = sum(r.f1 for r in all_results) / len(all_results)

    # Per-category statistics
    category_scores = {}
    for r in all_results:
        if r.category not in category_scores:
            category_scores[r.category] = []
        category_scores[r.category].append(r.f1)

    print(f"\n=== Results ===")
    print(f"Overall F1: {overall_f1:.3f}")
    print(f"Total QA pairs: {len(all_results)}")

    category_names = {
        1: "Multi-hop",
        2: "Single-hop",
        3: "Temporal",
        4: "Open-domain",
        5: "Adversarial"
    }

    for cat, scores in sorted(category_scores.items()):
        cat_name = category_names.get(cat, f"Category {cat}")
        cat_f1 = sum(scores) / len(scores)
        print(f"  {cat_name} (n={len(scores)}): {cat_f1:.3f}")

    # Save results
    output_data = {
        "overall_f1": overall_f1,
        "total_qa": len(all_results),
        "categories": {
            cat: {
                "name": category_names.get(cat, f"Category {cat}"),
                "count": len(scores),
                "f1": sum(scores) / len(scores)
            }
            for cat, scores in category_scores.items()
        },
        "results": [
            {
                "question": r.question,
                "ground_truth": r.ground_truth,
                "prediction": r.prediction,
                "f1": r.f1,
                "category": r.category
            }
            for r in all_results
        ]
    }

    with open(output_file, 'w') as f:
        json.dump(output_data, f, indent=2)

    print(f"\nResults saved to {output_file}")

    return overall_f1


def main():
    global SOCKET_PATH

    parser = argparse.ArgumentParser(description="LoCoMo benchmark for cc-soul")
    parser.add_argument('--data-file', required=True, help="Path to locomo10.json")
    parser.add_argument('--model', default="claude-3-5-sonnet-20241022", help="LLM model for answering")
    parser.add_argument('--output', default="locomo_results.json", help="Output file for results")
    parser.add_argument('--samples', nargs='*', help="Specific sample IDs to evaluate")
    parser.add_argument('--socket', help="Chitta daemon socket path")

    args = parser.parse_args()

    # Set socket path
    if args.socket:
        SOCKET_PATH = args.socket
    else:
        # Auto-detect socket
        import glob
        sockets = glob.glob("/tmp/chitta-*.sock")
        if sockets:
            SOCKET_PATH = sockets[0]
            print(f"Auto-detected socket: {SOCKET_PATH}")

    # Verify chitta is available
    if not os.path.exists(CHITTA_BIN):
        print(f"Error: chitta not found at {CHITTA_BIN}")
        sys.exit(1)

    run_benchmark(args.data_file, args.model, args.output, args.samples)


if __name__ == "__main__":
    main()
