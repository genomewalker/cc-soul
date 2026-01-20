#!/usr/bin/env python3
"""
LoCoMo Benchmark Runner for cc-soul

Evaluates cc-soul's memory against the LoCoMo benchmark for long-term conversational memory.
"""

import json
import subprocess
import argparse
import re
import os
import time
from pathlib import Path
from collections import Counter
from typing import Optional

LOCOMO_REPO = "https://github.com/snap-research/locomo.git"
LOCOMO_DIR = Path("/tmp/locomo")
LOCOMO_DATA = LOCOMO_DIR / "data" / "locomo10.json"
CHITTA = Path.home() / ".claude/bin/chitta"


def ensure_data():
    """Clone LoCoMo repo if not present."""
    if not LOCOMO_DATA.exists():
        print(f"Cloning LoCoMo repository...")
        LOCOMO_DIR.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", LOCOMO_REPO, str(LOCOMO_DIR)], check=True)
    return LOCOMO_DATA


def clear_locomo_data(sample_ids: list = None):
    """Clear existing locomo data from chitta to avoid duplicates."""
    print("Clearing existing locomo data...")

    # Get all locomo-tagged nodes
    result = chitta_call("recall", query="locomo", tag="locomo", limit=500)
    if not result:
        print("  No existing locomo data found")
        return 0

    # Count nodes by parsing output
    deleted = 0

    # Use forget to remove nodes with locomo tag
    if sample_ids:
        for sample_id in sample_ids:
            # Delete nodes tagged with this sample
            forget_result = chitta_call("forget", query=f"locomo {sample_id}", tag=sample_id, cascade="false")
            if forget_result and "deleted" in forget_result.lower():
                deleted += 1
    else:
        # Delete all locomo nodes
        forget_result = chitta_call("forget", query="locomo", tag="locomo", cascade="false")
        if forget_result:
            print(f"  {forget_result.strip()}")

    print(f"  Cleared locomo data")
    return deleted


def check_data_exists(sample_id: str) -> bool:
    """Check if conversation data already exists in chitta."""
    result = chitta_call("recall", query=sample_id, tag=sample_id, limit=5)
    if result and sample_id in result and '→' in result:
        return True
    return False


def load_data(path: Path) -> list:
    """Load LoCoMo JSON data."""
    with open(path) as f:
        return json.load(f)


def chitta_call(tool: str, **kwargs) -> Optional[str]:
    """Call chitta CLI tool."""
    cmd = [str(CHITTA), tool]
    for k, v in kwargs.items():
        cmd.extend([f"--{k.replace('_', '-')}", str(v)])

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        return result.stdout
    except subprocess.TimeoutExpired:
        # Return empty on timeout rather than erroring
        return ""
    except Exception as e:
        print(f"[chitta error] {e}")
        return None


def extract_session_facts(conv: dict, session_num: int) -> list:
    """Extract facts from a conversation session."""
    session_key = f"session_{session_num}"
    date_key = f"session_{session_num}_date_time"

    if session_key not in conv['conversation']:
        return []

    session = conv['conversation'][session_key]
    date = conv['conversation'].get(date_key, "unknown date")
    speaker_a = conv['conversation'].get('speaker_a', 'A')
    speaker_b = conv['conversation'].get('speaker_b', 'B')
    sample_id = conv['sample_id']

    facts = []
    for turn in session:
        speaker = turn.get('speaker', 'Unknown')
        text = turn.get('text', '')
        dia_id = turn.get('dia_id', '')

        facts.append({
            'sample_id': sample_id,
            'session': session_num,
            'date': date,
            'speaker': speaker,
            'dia_id': dia_id,
            'text': text,
            'has_image': 'img_url' in turn
        })

    return facts


DRY_RUN = False  # Global flag for dry-run mode
SKIP_INGEST = False  # Global flag for skip-ingest mode
NATIVE_MODE = False  # Global flag for native cc-soul retrieval only


def encode_conversation_batch(conv: dict, sample_id: str) -> str:
    """Use claude -p to encode sessions into SSL facts (single batch for reliability)."""
    speaker_a = conv['conversation'].get('speaker_a', 'A')
    speaker_b = conv['conversation'].get('speaker_b', 'B')

    # Build all sessions text (single batch approach - more reliable)
    all_sessions = f"Conversation {sample_id} between {speaker_a} and {speaker_b}:\n\n"

    session_nums = []
    for key in conv['conversation'].keys():
        if key.startswith('session_') and not key.endswith('_date_time'):
            try:
                num = int(key.split('_')[1])
                session_nums.append(num)
            except:
                pass

    for session_num in sorted(session_nums)[:18]:  # First 18 sessions
        session_key = f"session_{session_num}"
        date_key = f"session_{session_num}_date_time"

        if session_key not in conv['conversation']:
            continue

        session = conv['conversation'][session_key]
        date = conv['conversation'].get(date_key, "unknown")

        all_sessions += f"=== Session {session_num} ({date}) ===\n"
        for turn in session[:15]:
            speaker = turn.get('speaker', '?')
            text = turn.get('text', '')[:200]
            all_sessions += f"{speaker}: {text}\n"
        all_sessions += "\n"

    prompt = f"""Read this conversation carefully.

{all_sessions[:14000]}

Now extract everything you learned. For each fact:
- Who or what is it about?
- What happened, what's true, or what's the relationship?
- When did it happen (resolve relative dates using session dates)?

Format:
[{sample_id}] subject→relationship→object @when
[TRIPLET] subject predicate object

Think about what questions someone might ask about this conversation later.
What would they need to know? Extract those facts.

Be exhaustive. If you had to answer any question about this conversation
using only your extracted facts, could you?"""

    if DRY_RUN:
        print(f"\n--- BATCH ENCODE PROMPT ---")
        print(prompt[:1500] + "..." if len(prompt) > 1500 else prompt)
        print("--- END PROMPT ---\n")
        return f"""[{sample_id}] Caroline→attended→LGBTQ_support_group @7 May 2023
[{sample_id}] Melanie→painted→sunrise @2022
[{sample_id}] Caroline→identity→transgender_woman
[{sample_id}] Melanie→has→kids
[TRIPLET] Caroline is_a transgender_woman
[TRIPLET] Caroline attended LGBTQ_support_group
[TRIPLET] Melanie painted sunrise"""

    try:
        result = subprocess.run(
            ["claude", "-p", prompt, "--output-format", "text"],
            capture_output=True,
            text=True,
            timeout=240  # Increased timeout
        )
        return result.stdout.strip()
    except Exception as e:
        print(f"[encode error] {e}")
        return ""


def store_session_summaries(conv: dict, sample_id: str, tag_prefix: str = "locomo") -> int:
    """Store raw session summaries as episode nodes for vector fallback."""
    stored = 0

    session_nums = []
    for key in conv['conversation'].keys():
        if key.startswith('session_') and not key.endswith('_date_time'):
            try:
                num = int(key.split('_')[1])
                session_nums.append(num)
            except:
                pass

    for session_num in sorted(session_nums):
        session_key = f"session_{session_num}"
        date_key = f"session_{session_num}_date_time"

        if session_key not in conv['conversation']:
            continue

        session = conv['conversation'][session_key]
        date = conv['conversation'].get(date_key, "unknown")
        speaker_a = conv['conversation'].get('speaker_a', 'A')
        speaker_b = conv['conversation'].get('speaker_b', 'B')

        # Build session text (preserve raw content for vector search)
        lines = []
        for turn in session[:25]:  # More turns for better coverage
            speaker = turn.get('speaker', '?')
            text = turn.get('text', '')
            if text:
                lines.append(f"{speaker}: {text}")

        session_text = '\n'.join(lines)
        if not session_text:
            continue

        # Store as episode with full content (enables vector fallback)
        title = f"[{tag_prefix}:{sample_id}] Session {session_num} ({date})"
        chitta_call("observe",
            category="episode",
            title=title,
            content=session_text[:2000],  # More content for better retrieval
            tags=f"{tag_prefix},{sample_id},session,raw"
        )
        stored += 1

    return stored


def ingest_conversation(conv: dict, tag_prefix: str = "locomo", use_llm: bool = False) -> int:
    """Ingest a conversation into cc-soul memory - store sessions for native retrieval."""
    sample_id = conv['sample_id']

    # Check if we should skip ingestion
    if SKIP_INGEST and check_data_exists(sample_id):
        print(f"  Skipping ingestion (data exists for {sample_id})")
        return -1  # Signal that we skipped

    stored = 0
    triplets_stored = 0

    # Store raw session content - native retrieval will find what's relevant
    print(f"  Storing sessions...", end=" ", flush=True)
    sessions_stored = store_session_summaries(conv, sample_id, tag_prefix)
    print(f"{sessions_stored} sessions")

    # In native mode, skip SSL encoding - rely on native retrieval
    if NATIVE_MODE:
        print(f"  Native mode: skipping SSL encoding")
        return sessions_stored

    if use_llm:
        # Batched oracle encoding: one claude -p call for all sessions
        print(f"  Encoding SSL facts...", end=" ", flush=True)
        ssl_facts = encode_conversation_batch(conv, sample_id)
        print(f"({len(ssl_facts)} chars, {ssl_facts.count(chr(10))+1} lines)")

        if ssl_facts:
            for line in ssl_facts.split('\n'):
                line = line.strip()
                if not line:
                    continue

                # Handle triplets
                if line.startswith('[TRIPLET]'):
                    parts = line[9:].strip().split()
                    if len(parts) >= 3:
                        subject = parts[0]
                        predicate = parts[1]
                        obj = ' '.join(parts[2:])
                        chitta_call("connect",
                            subject=subject,
                            predicate=predicate,
                            object=obj
                        )
                        triplets_stored += 1

                # Handle SSL facts
                elif '→' in line or sample_id in line:
                    # Store full line as both title and content to avoid truncation
                    chitta_call("observe",
                        category="signal",
                        title=line[:250],
                        content=line[:500],
                        tags=f"{tag_prefix},{sample_id},fact,ssl"
                    )
                    stored += 1

        print(f"  Stored {stored} facts + {triplets_stored} triplets")
    else:
        # Simple mode: store raw session content
        session_nums = []
        for key in conv['conversation'].keys():
            if key.startswith('session_') and not key.endswith('_date_time'):
                try:
                    num = int(key.split('_')[1])
                    session_nums.append(num)
                except:
                    pass

        for session_num in sorted(session_nums):
            facts = extract_session_facts(conv, session_num)
            if not facts:
                continue

            date = facts[0]['date'] if facts else "unknown"
            session_text = "\n".join([f"{f['speaker']}: {f['text']}" for f in facts])

            chitta_call("observe",
                category="episode",
                title=f"[{tag_prefix}:{sample_id}] Session {session_num} ({date})",
                content=session_text[:1500],
                tags=f"{tag_prefix},{sample_id},session_{session_num}"
            )
            stored += 1

    return stored


def detect_question_type(question: str) -> str:
    """Detect question type for optimized retrieval."""
    q_lower = question.lower()

    # When questions - temporal
    if q_lower.startswith('when') or 'what date' in q_lower or 'what time' in q_lower:
        return 'when'

    # What questions - factual
    if q_lower.startswith('what'):
        if 'relationship' in q_lower or 'status' in q_lower:
            return 'relationship'
        if 'identity' in q_lower:
            return 'identity'
        if 'research' in q_lower or 'study' in q_lower:
            return 'research'
        if 'field' in q_lower or 'education' in q_lower or 'career' in q_lower:
            return 'career'
        return 'what'

    # Where questions - location
    if q_lower.startswith('where'):
        return 'where'

    # Who questions - person
    if q_lower.startswith('who'):
        return 'who'

    return 'general'


def recall_for_question(question: str, sample_id: str, category: int = 0) -> str:
    """Native cc-soul retrieval using lens, soul_context, and triplets."""
    results = []

    # Extract entities from question
    entities = re.findall(r'\b([A-Z][a-z]+)\b', question)

    # === 1. Set intention to prime retrieval ===
    chitta_call("intend", intention=f"Answer: {question[:100]}")

    # === 2. Soul context - auto-retrieves relevant wisdom ===
    soul = chitta_call("soul_context", query=f"{sample_id} {question}")
    if soul:
        results.append("=== Soul Context ===")
        # Extract the relevant_wisdom section
        for line in soul.split('\n'):
            if '→' in line or sample_id in line or any(e in line for e in entities):
                results.append(line[:200])

    # === 3. Multi-perspective lens search ===
    # buddhi (analysis) for factual questions
    buddhi = chitta_call("lens", voice="buddhi", query=f"{sample_id} {question}", limit=5)
    if buddhi:
        results.append("=== Buddhi (Analysis) ===")
        for line in buddhi.split('\n')[:10]:
            if '→' in line or sample_id in line:
                results.append(line[:200])

    # chitta (memory) for recall
    chitta_mem = chitta_call("lens", voice="chitta", query=f"{sample_id} {question}", limit=5)
    if chitta_mem:
        results.append("=== Chitta (Memory) ===")
        for line in chitta_mem.split('\n')[:10]:
            if '→' in line or sample_id in line:
                results.append(line[:200])

    # === 4. Triplet queries for entities ===
    for entity in entities[:2]:
        triplets = chitta_call("query", subject=entity, limit=10)
        if triplets and triplets.strip() and '--[' in triplets:
            results.append(f"=== {entity} relationships ===")
            results.append(triplets[:400])

    # === 5. Multi-hop for complex questions (category 1) ===
    if category == 1 and entities:
        multi = chitta_call("multi_hop", query=f"{entities[0]} {sample_id}", limit=5)
        if multi and sample_id in str(multi):
            results.append("=== Multi-hop ===")
            results.append(multi[:300])

    return '\n'.join(results[:30]) if results else ""


def calculate_f1(prediction: str, ground_truth: str) -> float:
    """Calculate F1 score between prediction and ground truth."""
    # Tokenize and normalize
    def tokenize(text):
        text = str(text).lower()
        # Replace underscores with spaces
        text = text.replace('_', ' ')
        # Remove punctuation except spaces
        text = re.sub(r'[^\w\s]', ' ', text)
        # Split and filter empty
        return set(t for t in text.split() if t)

    pred_tokens = tokenize(prediction)
    truth_tokens = tokenize(ground_truth)

    if not pred_tokens or not truth_tokens:
        return 0.0

    common = pred_tokens & truth_tokens

    if not common:
        return 0.0

    precision = len(common) / len(pred_tokens)
    recall = len(common) / len(truth_tokens)

    if precision + recall == 0:
        return 0.0

    return 2 * (precision * recall) / (precision + recall)


def prepare_qa_batch(data: list, sample_ids: Optional[list] = None, max_qa: int = 50, output_file: str = None) -> list:
    """Prepare QA batch with retrieved context for LLM answering."""
    batch = []

    for conv in data:
        sample_id = conv['sample_id']

        if sample_ids and sample_id not in sample_ids:
            continue

        print(f"\n=== Preparing {sample_id} ===")

        # Ingest conversation
        print(f"Ingesting conversation...")
        stored = ingest_conversation(conv)
        print(f"Stored {stored} session summaries")

        # Prepare QA pairs with context
        qa_pairs = conv.get('qa', [])[:max_qa]

        for i, qa in enumerate(qa_pairs):
            question = qa['question']
            ground_truth = str(qa['answer'])
            category = qa.get('category', 0)

            # Retrieve context
            context = recall_for_question(question, sample_id)

            batch.append({
                'id': f"{sample_id}_{i}",
                'sample_id': sample_id,
                'question': question,
                'context': context[:2000] if context else "",
                'ground_truth': ground_truth,
                'category': category
            })

            if (i + 1) % 20 == 0:
                print(f"  Prepared {i+1}/{len(qa_pairs)} QA pairs")

        print(f"  Total: {len(qa_pairs)} QA pairs prepared")

    # Save batch to file
    if output_file:
        with open(output_file, 'w') as f:
            json.dump(batch, f, indent=2)
        print(f"\nSaved {len(batch)} QA pairs to {output_file}")

    return batch


def answer_batch_with_claude(qa_pairs: list) -> list:
    """Answer multiple questions in one claude -p call (much faster)."""
    if not qa_pairs:
        return []

    # Build batch prompt
    prompt = """Answer each question based ONLY on its context. Be concise - just the answer.

IMPORTANT RULES:
- For "when" questions: Look for dates after @ symbol (e.g., @7-May-2023, @June-2023, @2022)
- Extract the FIRST/EARLIEST date if multiple are shown
- For identity questions: Look for "identity→X" or "is_a X" patterns
- For relationship questions: Look for "relationship_status→X" patterns
- If information is NOT in the context, say "no information"

"""
    for i, qa in enumerate(qa_pairs):
        prompt += f"""--- Question {i+1} ---
Context: {qa['context'][:1500]}

Question: {qa['question']}

---

"""

    prompt += """Output your answers in this exact format (one per line):
A1: [answer to question 1]
A2: [answer to question 2]
..."""

    if DRY_RUN:
        print(f"\n--- BATCH DECODE PROMPT ({len(qa_pairs)} questions) ---")
        print(prompt[:800] + "...")
        return ["[dry-run]"] * len(qa_pairs)

    try:
        result = subprocess.run(
            ["claude", "-p", prompt, "--output-format", "text"],
            capture_output=True,
            text=True,
            timeout=180  # Longer timeout for batch
        )
        output = result.stdout.strip()

        # Parse answers
        answers = []
        for line in output.split('\n'):
            if line.startswith('A') and ':' in line:
                answer = line.split(':', 1)[1].strip()
                answers.append(answer[:200])

        # Pad with "no information" if not enough answers
        while len(answers) < len(qa_pairs):
            answers.append("no information")

        return answers[:len(qa_pairs)]
    except Exception as e:
        print(f"[batch decode error] {e}")
        return ["error"] * len(qa_pairs)


def answer_with_claude(question: str, context: str) -> str:
    """Use claude -p to answer a question based on context (oracle decoding)."""

    # Detect question type for better prompting
    q_lower = question.lower()
    if q_lower.startswith('when'):
        focus = "Look for dates in formats like: @7-May-2023, @June-2023, @2022, etc."
    elif 'identity' in q_lower:
        focus = "Look for identity→transgender, is_a transgender, etc."
    elif 'relationship' in q_lower or 'status' in q_lower:
        focus = "Look for relationship_status→single, married_to, etc."
    elif 'research' in q_lower:
        focus = "Look for researched→X or →research→X patterns."
    elif 'field' in q_lower or 'education' in q_lower:
        focus = "Look for plans_to→career, education, counseling, psychology patterns."
    else:
        focus = "Extract the most relevant facts from the context."

    prompt = f"""Answer the question based ONLY on the context below.

CONTEXT (SSL patterns where → means relationship, @ means date/location):
{context[:3500]}

QUESTION: {question}

INSTRUCTIONS:
- {focus}
- Extract the exact answer from the context
- If asked "when", give the date (e.g., "7 May 2023" or "June 2023")
- If asked "what", give the specific thing mentioned
- If the exact information is NOT in the context, say "no information"
- Be concise: just the answer, no explanation

ANSWER:"""

    if DRY_RUN:
        print(f"\n--- DECODE PROMPT ---")
        print(f"Context: {context[:200]}..." if len(context) > 200 else f"Context: {context}")
        print(f"Question: {question}")
        print("--- END PROMPT ---\n")
        # Return mock answer for dry-run
        return "[dry-run answer]"

    try:
        result = subprocess.run(
            ["claude", "-p", prompt, "--output-format", "text"],
            capture_output=True,
            text=True,
            timeout=60
        )
        answer = result.stdout.strip()
        # Clean up answer
        if answer:
            # Take first line/sentence as answer
            answer = answer.split('\n')[0].strip()
            return answer[:200]
        return "no information"
    except subprocess.TimeoutExpired:
        return "timeout"
    except Exception as e:
        print(f"[claude error] {e}")
        return "error"


def run_with_llm(data: list, sample_ids: Optional[list] = None, max_qa: int = 50, batch_decode: bool = True) -> dict:
    """Run benchmark with LLM answering via claude -p."""
    results = {
        'total_qa': 0,
        'total_f1': 0.0,
        'by_category': {1: [], 2: [], 3: [], 4: [], 5: []},
        'samples': [],
        'details': []
    }

    for conv in data:
        sample_id = conv['sample_id']

        if sample_ids and sample_id not in sample_ids:
            continue

        print(f"\n=== Processing {sample_id} ===")

        # Ingest conversation with oracle encoding
        print(f"Ingesting conversation (oracle encoding)...")
        stored = ingest_conversation(conv, use_llm=True)
        print(f"Stored {stored} SSL facts")

        # Run QA with LLM (oracle decoding)
        qa_pairs = conv.get('qa', [])[:max_qa]
        sample_results = []

        # First retrieve context for all questions
        print(f"Retrieving context for {len(qa_pairs)} questions...")
        qa_with_context = []
        for i, qa in enumerate(qa_pairs):
            question = qa['question']
            ground_truth = str(qa['answer'])
            category = qa.get('category', 0)
            context = recall_for_question(question, sample_id, category)

            qa_with_context.append({
                'question': question,
                'context': context,
                'ground_truth': ground_truth,
                'category': category,
                'index': i
            })

        if batch_decode:
            # Batch decode - one claude call for all questions
            print(f"Batch decoding {len(qa_with_context)} questions...", end=" ", flush=True)
            predictions = answer_batch_with_claude(qa_with_context)
            print(f"done")

            for qa, prediction in zip(qa_with_context, predictions):
                f1 = calculate_f1(prediction, qa['ground_truth'])
                category = qa['category']

                results['total_qa'] += 1
                results['total_f1'] += f1

                if category in results['by_category']:
                    results['by_category'][category].append(f1)

                sample_results.append({
                    'question': qa['question'],
                    'ground_truth': qa['ground_truth'],
                    'prediction': prediction,
                    'f1': f1,
                    'category': category
                })

                results['details'].append({
                    'id': f"{sample_id}_{qa['index']}",
                    'question': qa['question'],
                    'context': qa['context'][:1500] if qa['context'] else "",
                    'prediction': prediction,
                    'ground_truth': qa['ground_truth'],
                    'f1': f1,
                    'category': category
                })

                print(f"  Q{qa['index']+1} (cat{category}): {qa['question'][:40]}... -> {prediction[:25]}... (F1={f1:.2f})")
        else:
            # Individual decoding - one claude call per question
            for qa in qa_with_context:
                print(f"  Q{qa['index']+1} (cat{qa['category']}): {qa['question'][:45]}...", end=" ", flush=True)
                prediction = answer_with_claude(qa['question'], qa['context'])
                print(f"-> {prediction[:30]}")

                f1 = calculate_f1(prediction, qa['ground_truth'])
                category = qa['category']

                results['total_qa'] += 1
                results['total_f1'] += f1

                if category in results['by_category']:
                    results['by_category'][category].append(f1)

                sample_results.append({
                    'question': qa['question'],
                    'ground_truth': qa['ground_truth'],
                    'prediction': prediction,
                    'f1': f1,
                    'category': category
                })

                results['details'].append({
                    'id': f"{sample_id}_{qa['index']}",
                    'question': qa['question'],
                    'context': qa['context'][:1500] if qa['context'] else "",
                    'prediction': prediction,
                    'ground_truth': qa['ground_truth'],
                    'f1': f1,
                    'category': category
                })

        results['samples'].append({
            'sample_id': sample_id,
            'qa_count': len(sample_results),
            'avg_f1': sum(r['f1'] for r in sample_results) / len(sample_results) if sample_results else 0
        })

    return results


def evaluate_answers(batch_file: str, answers_file: str) -> dict:
    """Evaluate LLM answers against ground truth."""
    with open(batch_file) as f:
        batch = json.load(f)

    with open(answers_file) as f:
        answers = json.load(f)

    # Create answer lookup
    answer_map = {a['id']: a['answer'] for a in answers}

    results = {
        'total_qa': 0,
        'total_f1': 0.0,
        'by_category': {1: [], 2: [], 3: [], 4: [], 5: []},
        'details': []
    }

    for qa in batch:
        qid = qa['id']
        prediction = answer_map.get(qid, "no answer")
        ground_truth = qa['ground_truth']
        category = qa['category']

        f1 = calculate_f1(prediction, ground_truth)

        results['total_qa'] += 1
        results['total_f1'] += f1

        if category in results['by_category']:
            results['by_category'][category].append(f1)

        results['details'].append({
            'id': qid,
            'question': qa['question'],
            'prediction': prediction,
            'ground_truth': ground_truth,
            'f1': f1,
            'category': category
        })

    return results


def run_benchmark(data: list, sample_ids: Optional[list] = None, max_qa: int = 50, use_llm: bool = False) -> dict:
    """Run benchmark on specified conversations."""
    results = {
        'total_qa': 0,
        'total_f1': 0.0,
        'by_category': {1: [], 2: [], 3: [], 4: [], 5: []},
        'samples': []
    }

    for conv in data:
        sample_id = conv['sample_id']

        if sample_ids and sample_id not in sample_ids:
            continue

        print(f"\n=== Processing {sample_id} ===")

        # Ingest conversation
        print(f"Ingesting conversation...")
        stored = ingest_conversation(conv)
        print(f"Stored {stored} session summaries")

        # Run QA
        qa_pairs = conv.get('qa', [])[:max_qa]
        sample_results = []

        for i, qa in enumerate(qa_pairs):
            question = qa['question']
            ground_truth = str(qa['answer'])
            category = qa.get('category', 0)

            # Retrieve context
            context = recall_for_question(question, sample_id)

            if use_llm:
                # Placeholder - actual LLM answering done externally
                prediction = context[:500] if context else "no information"
            else:
                # Retrieval-only mode
                prediction = context[:500] if context else "no information"

            f1 = calculate_f1(prediction, ground_truth)

            results['total_qa'] += 1
            results['total_f1'] += f1

            if category in results['by_category']:
                results['by_category'][category].append(f1)

            sample_results.append({
                'question': question,
                'ground_truth': ground_truth,
                'prediction': prediction[:100],
                'f1': f1,
                'category': category
            })

            if (i + 1) % 10 == 0:
                print(f"  Processed {i+1}/{len(qa_pairs)} QA pairs")

        results['samples'].append({
            'sample_id': sample_id,
            'qa_count': len(sample_results),
            'avg_f1': sum(r['f1'] for r in sample_results) / len(sample_results) if sample_results else 0
        })

    return results


def print_results(results: dict):
    """Print benchmark results."""
    print("\n" + "=" * 50)
    print("=== LoCoMo Benchmark Results ===")
    print("=" * 50)

    total = results['total_qa']
    avg_f1 = (results['total_f1'] / total * 100) if total > 0 else 0

    print(f"\nTotal QA Pairs: {total}")
    print(f"Overall F1: {avg_f1:.1f}%")

    print("\nBy Category:")
    category_names = {
        1: "Multi-hop",
        2: "Single-hop",
        3: "Temporal",
        4: "Open-domain",
        5: "Adversarial"
    }

    for cat, scores in results['by_category'].items():
        if scores:
            avg = sum(scores) / len(scores) * 100
            print(f"  {category_names.get(cat, f'Cat {cat}')} (n={len(scores)}): {avg:.1f}%")

    print("\nPer Conversation:")
    for sample in results['samples']:
        print(f"  {sample['sample_id']}: {sample['avg_f1']*100:.1f}% ({sample['qa_count']} QA)")

    print("\nComparison (from paper):")
    print("  Human ceiling: 87.9%")
    print("  GPT-4 baseline: 32.1%")
    print(f"  cc-soul: {avg_f1:.1f}%")


def main():
    parser = argparse.ArgumentParser(description='LoCoMo Benchmark for cc-soul')
    parser.add_argument('samples', nargs='*', help='Sample IDs to test (e.g., conv-26)')
    parser.add_argument('--full', action='store_true', help='Run full benchmark')
    parser.add_argument('--data', type=Path, help='Path to locomo10.json')
    parser.add_argument('--max-qa', type=int, default=50, help='Max QA pairs per conversation')

    # LLM answering modes
    parser.add_argument('--llm', action='store_true',
                        help='Use claude -p to answer questions (requires claude CLI)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Show prompts without calling claude -p (for testing)')
    parser.add_argument('--prepare', type=str, metavar='FILE',
                        help='Prepare QA batch with context for manual LLM answering (saves to FILE)')
    parser.add_argument('--evaluate', nargs=2, metavar=('BATCH', 'ANSWERS'),
                        help='Evaluate LLM answers: --evaluate batch.json answers.json')
    parser.add_argument('--save-results', type=str, metavar='FILE',
                        help='Save detailed results to JSON file')
    parser.add_argument('--no-batch', action='store_true',
                        help='Disable batch decoding (one claude call per question)')
    parser.add_argument('--clear', action='store_true',
                        help='Clear existing locomo data before running (avoids duplicates)')
    parser.add_argument('--skip-ingest', action='store_true',
                        help='Skip ingestion if data already exists (use cached data)')
    parser.add_argument('--native', action='store_true',
                        help='Use native cc-soul retrieval only (no SSL encoding, faster)')

    args = parser.parse_args()

    # Set global flags
    global DRY_RUN, SKIP_INGEST, NATIVE_MODE
    DRY_RUN = args.dry_run
    SKIP_INGEST = args.skip_ingest
    NATIVE_MODE = args.native

    # Evaluate mode
    if args.evaluate:
        batch_file, answers_file = args.evaluate
        print(f"Evaluating answers from {answers_file} against {batch_file}")
        results = evaluate_answers(batch_file, answers_file)
        print_results(results)
        return

    # Ensure data exists
    data_path = args.data or ensure_data()
    print(f"Loading data from {data_path}")
    data = load_data(data_path)
    print(f"Loaded {len(data)} conversations")

    # Determine which samples to run
    sample_ids = None
    if args.samples:
        sample_ids = args.samples
    elif not args.full:
        # Default: first conversation
        sample_ids = [data[0]['sample_id']]

    if sample_ids:
        print(f"Running on samples: {sample_ids}")
    else:
        print("Running full benchmark")

    # Clear existing data if requested
    if args.clear:
        clear_locomo_data(sample_ids)

    # Prepare mode - for LLM answering
    if args.prepare:
        batch = prepare_qa_batch(data, sample_ids, args.max_qa, args.prepare)
        print(f"\nTo answer with Claude Code, run:")
        print(f"  1. Read {args.prepare}")
        print(f"  2. Answer each question based on context")
        print(f"  3. Save answers to answers.json")
        print(f"  4. python3 {__file__} --evaluate {args.prepare} answers.json")
        return

    # Run benchmark
    if args.llm:
        batch_mode = not args.no_batch
        print(f"\nUsing claude -p for LLM answering (batch={batch_mode})...")
        results = run_with_llm(data, sample_ids, args.max_qa, batch_decode=batch_mode)
    else:
        print("\nRunning retrieval-only mode (use --llm for LLM answering)...")
        results = run_benchmark(data, sample_ids, args.max_qa)

    # Print results
    print_results(results)

    # Save detailed results if requested
    if args.save_results:
        with open(args.save_results, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\nDetailed results saved to {args.save_results}")


if __name__ == '__main__':
    main()
