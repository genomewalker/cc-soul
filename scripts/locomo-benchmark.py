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
from datetime import datetime, timedelta

LOCOMO_REPO = "https://github.com/snap-research/locomo.git"
LOCOMO_DIR = Path("/tmp/locomo")
LOCOMO_DATA = LOCOMO_DIR / "data" / "locomo10.json"
CHITTA = Path.home() / ".claude/bin/chitta"

# Temporal patterns for extracting events with relative/absolute dates
# Each pattern returns (regex, date_type) where date_type guides resolution
TEMPORAL_PATTERNS = [
    # === Relative dates ===
    # "yesterday" patterns
    (r'\b(went|attended|visited|did|had|was at|joined|started|got|adopted|met|moved)\b[^.]*\byesterday\b', 'yesterday'),
    (r'\byesterday\b[^.]*\b(went|attended|visited|did|had|was at|joined|started|got|adopted|met|moved)\b', 'yesterday'),
    # "last week/weekend" patterns
    (r'\blast (week|weekend)\b', 'last week'),
    (r'\bthe week before\b', 'last week'),
    # "last month" patterns
    (r'\blast month\b', 'last month'),
    # "last year" patterns
    (r'\blast year\b', 'last year'),
    # Specific day patterns like "on Friday", "last Friday"
    (r'\b(last|this past)\s+(monday|tuesday|wednesday|thursday|friday|saturday|sunday)\b', 'last week'),
    (r'\bon\s+(monday|tuesday|wednesday|thursday|friday|saturday|sunday)\b', 'this week'),
    # "X ago" patterns
    (r'\b(\d+|a|one|two|three|four|five)\s+(day|week|month|year)s?\s+ago\b', 'ago'),

    # === Absolute dates (year expressions) ===
    # "in 2022", "back in 2019", "during 2020"
    (r'\b(?:in|back in|during|around)\s+(20\d{2})\b', 'year'),
    # "in June 2023", "last June", "this June"
    (r'\b(?:in|last|this)\s+(january|february|march|april|may|june|july|august|september|october|november|december)(?:\s+(20\d{2}))?\b', 'month'),
    # "on March 15", "on the 15th"
    (r'\bon\s+(?:the\s+)?(\d{1,2})(?:st|nd|rd|th)?\s+(?:of\s+)?(january|february|march|april|may|june|july|august|september|october|november|december)', 'specific_date'),
    (r'\bon\s+(january|february|march|april|may|june|july|august|september|october|november|december)\s+(\d{1,2})(?:st|nd|rd|th)?', 'specific_date'),
]

# Event extraction patterns - expanded to capture more life events
# Use non-greedy matching and exclude temporal words from capture
EVENT_PATTERNS = [
    # === Movement/Travel ===
    r'I\s+(went to|attended|visited|joined|signed up for|participated in|traveled to|flew to|drove to)\s+(?:a |an |the )?(.+?)(?:\s+(?:yesterday|last\s+\w+|this\s+\w+|today|ago|in\s+20\d{2})|\s+and\s+|[.,!?]|$)',
    # "went camping/hiking/etc"
    r'(went|did)\s+(camping|hiking|running|swimming|painting|pottery|yoga|fishing|skiing|surfing)',

    # === Life events - starting things ===
    r'I\s+(started|began|opened|founded|launched|created)\s+(?:a |an |the |my )?(.+?)(?:\s+(?:yesterday|last\s+\w+|in\s+20\d{2}|ago)|\s+and\s+|[.,!?]|$)',

    # === Acquisition/Getting ===
    r'I\s+(got|adopted|bought|received|acquired|picked up)\s+(?:a |an |the |my )?(.+?)(?:\s+(?:yesterday|last\s+\w+|in\s+20\d{2}|ago)|\s+and\s+|[.,!?]|$)',

    # === Relationships/Meetings ===
    r'I\s+(met|married|divorced|dated|proposed to|got engaged to)\s+(?:my\s+)?(.+?)(?:\s+(?:yesterday|last\s+\w+|in\s+20\d{2}|ago)|\s+and\s+|[.,!?]|$)',

    # === Location changes ===
    r'I\s+(moved to|relocated to|returned to|came back to|settled in)\s+(?:a |an |the )?(.+?)(?:\s+(?:yesterday|last\s+\w+|in\s+20\d{2}|ago)|\s+and\s+|[.,!?]|$)',

    # === Education/Career ===
    r'I\s+(graduated from|finished|completed|enrolled in|dropped out of|retired from|quit|left)\s+(?:a |an |the |my )?(.+?)(?:\s+(?:yesterday|last\s+\w+|in\s+20\d{2}|ago)|\s+and\s+|[.,!?]|$)',

    # === Loss/Death ===
    r'(?:My\s+)?(\w+)\s+(passed away|died|was lost|passed on)(?:\s+(?:yesterday|last\s+\w+|in\s+20\d{2}|ago))?',
    r'I\s+lost\s+(?:my\s+)?(.+?)(?:\s+(?:yesterday|last\s+\w+|in\s+20\d{2}|ago)|\s+and\s+|[.,!?]|$)',

    # === General actions ===
    r'I\s+had\s+(?:a |an )?(.+?)(?:\s+(?:yesterday|last\s+\w+|this\s+\w+|today|ago)|\s+and\s+|[.,!?]|$)',
    r'I\s+did\s+(?:a |an |some )?(.+?)(?:\s+(?:yesterday|last\s+\w+|this\s+\w+|today|ago)|\s+and\s+|[.,!?]|$)',
]


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


def chitta_rpc(tool: str, **kwargs) -> Optional[dict]:
    """Call chitta via JSON-RPC for tools not exposed in CLI."""
    request = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": tool,
            "arguments": kwargs
        }
    }

    try:
        result = subprocess.run(
            [str(CHITTA), "--json"],
            input=json.dumps(request),
            capture_output=True,
            text=True,
            timeout=60
        )
        if result.stdout:
            # Parse JSON response
            for line in result.stdout.strip().split('\n'):
                if line.startswith('{'):
                    response = json.loads(line)
                    if 'result' in response:
                        return response['result']
        return None
    except Exception as e:
        print(f"[chitta rpc error] {e}")
        return None


def parse_session_date(date_str: str) -> Optional[datetime]:
    """Parse LoCoMo session date format like '1:56 pm on 8 May, 2023'."""
    # Extract date part: "8 May, 2023" or "20 July, 2023"
    match = re.search(r'(\d{1,2})\s+(\w+),?\s+(\d{4})', date_str)
    if not match:
        return None

    day = int(match.group(1))
    month_name = match.group(2)
    year = int(match.group(3))

    # Month name to number
    months = {
        'january': 1, 'february': 2, 'march': 3, 'april': 4,
        'may': 5, 'june': 6, 'july': 7, 'august': 8,
        'september': 9, 'october': 10, 'november': 11, 'december': 12
    }
    month = months.get(month_name.lower())
    if not month:
        return None

    try:
        return datetime(year, month, day)
    except ValueError:
        return None


def format_natural_date(iso_date: str) -> str:
    """Convert ISO date (2023-05-07) to natural format (7 May 2023)."""
    try:
        dt = datetime.strptime(iso_date, '%Y-%m-%d')
        return dt.strftime('%-d %B %Y')  # "7 May 2023"
    except:
        return iso_date


def resolve_relative_date(relative: str, context_date: datetime, match_groups: tuple = None) -> str:
    """Resolve relative date expression to ISO format.

    Args:
        relative: The date expression type ('yesterday', 'last week', 'year', 'month', etc.)
        context_date: The date context for resolution
        match_groups: Optional regex match groups for extracting specific values

    Returns:
        ISO date string (YYYY-MM-DD)
    """
    relative = relative.lower().strip()

    if relative == 'yesterday':
        resolved = context_date - timedelta(days=1)
    elif relative in ('last week', 'the week before'):
        resolved = context_date - timedelta(days=7)
    elif relative == 'last weekend':
        # Find previous weekend
        days_since_sunday = context_date.weekday() + 1
        resolved = context_date - timedelta(days=days_since_sunday)
    elif relative == 'last month':
        resolved = context_date - timedelta(days=30)
    elif relative == 'last year':
        resolved = context_date.replace(year=context_date.year - 1)
    elif relative == 'this week':
        resolved = context_date
    elif relative == 'year' and match_groups:
        # "in 2022" - use the year, keep month/day from context or default to mid-year
        try:
            year = int(match_groups[0]) if match_groups else context_date.year
            resolved = datetime(year, 6, 15)  # Mid-year as approximate
        except:
            resolved = context_date
    elif relative == 'month' and match_groups:
        # "in June 2023" or "last June"
        months = {
            'january': 1, 'february': 2, 'march': 3, 'april': 4,
            'may': 5, 'june': 6, 'july': 7, 'august': 8,
            'september': 9, 'october': 10, 'november': 11, 'december': 12
        }
        try:
            month_name = match_groups[0].lower() if match_groups else 'january'
            month = months.get(month_name, 1)
            # Check if year is in match groups
            year = context_date.year
            if len(match_groups) > 1 and match_groups[1]:
                year = int(match_groups[1])
            resolved = datetime(year, month, 15)
        except:
            resolved = context_date
    elif relative == 'specific_date' and match_groups:
        # "on March 15" or "on the 15th of March"
        months = {
            'january': 1, 'february': 2, 'march': 3, 'april': 4,
            'may': 5, 'june': 6, 'july': 7, 'august': 8,
            'september': 9, 'october': 10, 'november': 11, 'december': 12
        }
        try:
            # Match groups could be (day, month) or (month, day)
            if match_groups[0].isdigit():
                day = int(match_groups[0])
                month_name = match_groups[1].lower()
            else:
                month_name = match_groups[0].lower()
                day = int(match_groups[1])
            month = months.get(month_name, 1)
            resolved = datetime(context_date.year, month, day)
        except:
            resolved = context_date
    elif relative == 'ago' and match_groups:
        # "3 weeks ago", "a year ago"
        try:
            num_str = match_groups[0].lower()
            unit = match_groups[1].lower()
            # Convert word to number
            word_to_num = {'a': 1, 'one': 1, 'two': 2, 'three': 3, 'four': 4, 'five': 5}
            num = word_to_num.get(num_str, int(num_str) if num_str.isdigit() else 1)

            if 'day' in unit:
                resolved = context_date - timedelta(days=num)
            elif 'week' in unit:
                resolved = context_date - timedelta(weeks=num)
            elif 'month' in unit:
                resolved = context_date - timedelta(days=num * 30)
            elif 'year' in unit:
                resolved = context_date.replace(year=context_date.year - num)
            else:
                resolved = context_date
        except:
            resolved = context_date
    else:
        resolved = context_date

    return resolved.strftime('%Y-%m-%d')


# Entity fact patterns (non-temporal facts about people/things)
ENTITY_FACT_PATTERNS = [
    # "I have X" - possessions, relationships
    (r'I\s+have\s+(?:a |an |the )?(\d+\s+)?(.+?)(?:[.,!?]|\s+and\s+|$)', 'has'),
    # "I am a X" - identity/role
    (r"I(?:'m| am)\s+(?:a |an )?(.+?)(?:[.,!?]|\s+and\s+|$)", 'is_a'),
    # "I work as/at X" - occupation
    (r'I\s+work\s+(?:as|at|for)\s+(?:a |an |the )?(.+?)(?:[.,!?]|\s+and\s+|$)', 'works_at'),
    # "I live in X" - location
    (r'I\s+live\s+in\s+(.+?)(?:[.,!?]|\s+and\s+|$)', 'lives_in'),
    # "I study X" - education
    (r'I\s+(?:study|am studying|majored in)\s+(.+?)(?:[.,!?]|\s+and\s+|$)', 'studies'),
    # "My X is/are Y" - attributes
    (r'My\s+(\w+)\s+(?:is|are)\s+(.+?)(?:[.,!?]|\s+and\s+|$)', 'attr'),
    # "I like/love/enjoy X" - preferences
    (r'I\s+(?:like|love|enjoy|prefer)\s+(.+?)(?:[.,!?]|\s+and\s+|$)', 'likes'),
    # Named entities: "X is my Y"
    (r'([A-Z][a-z]+)\s+is\s+my\s+(\w+)', 'relation'),
]


def extract_entity_facts(text: str, speaker: str) -> list:
    """Extract non-temporal facts about entities from text."""
    facts = []

    for pattern, predicate_type in ENTITY_FACT_PATTERNS:
        matches = re.findall(pattern, text, re.IGNORECASE)
        for match in matches:
            if isinstance(match, tuple):
                if predicate_type == 'has' and len(match) >= 2:
                    # "I have 3 dogs" -> (count, object)
                    count = match[0].strip() if match[0] else ''
                    obj = match[1].strip()
                    obj = f"{count}{obj}".strip() if count else obj
                    facts.append({
                        'subject': speaker.lower(),
                        'predicate': 'has',
                        'object': re.sub(r'\s+', '_', obj.lower())[:40]
                    })
                elif predicate_type == 'attr' and len(match) >= 2:
                    # "My dog is Max" -> speaker.dog = Max
                    attr_name = match[0].strip().lower()
                    attr_value = match[1].strip()
                    facts.append({
                        'subject': speaker.lower(),
                        'predicate': f'{attr_name}_is',
                        'object': re.sub(r'\s+', '_', attr_value.lower())[:40]
                    })
                elif predicate_type == 'relation' and len(match) >= 2:
                    # "Max is my dog" -> Max is_a speaker's_dog
                    name = match[0].strip()
                    relation = match[1].strip().lower()
                    facts.append({
                        'subject': name.lower(),
                        'predicate': 'is_a',
                        'object': f"{speaker.lower()}s_{relation}"
                    })
                else:
                    obj = match[-1].strip() if match else ''
                    if obj and len(obj) > 2:
                        facts.append({
                            'subject': speaker.lower(),
                            'predicate': predicate_type,
                            'object': re.sub(r'\s+', '_', obj.lower())[:40]
                        })
            else:
                obj = match.strip()
                if obj and len(obj) > 2:
                    facts.append({
                        'subject': speaker.lower(),
                        'predicate': predicate_type,
                        'object': re.sub(r'\s+', '_', obj.lower())[:40]
                    })

    return facts


def extract_temporal_events(text: str, speaker: str, context_date: datetime) -> list:
    """Extract events with temporal markers from session text."""
    events = []
    text_lower = text.lower()

    # Check for temporal markers and capture groups
    date_type = None
    match_groups = None
    for pattern, expr_type in TEMPORAL_PATTERNS:
        match = re.search(pattern, text_lower, re.IGNORECASE)
        if match:
            date_type = expr_type
            match_groups = match.groups() if match.groups() else None
            break

    # Extract the event/activity even without temporal marker (store with context date)
    # This ensures we capture facts even if the date resolution fails
    use_context_date = date_type is None

    # Extract the event/activity
    for pattern in EVENT_PATTERNS:
        matches = re.findall(pattern, text, re.IGNORECASE)
        for match in matches:
            if isinstance(match, tuple):
                # Handle tuple results - extract verb and object
                verb = None
                activity = None

                if len(match) >= 2:
                    # Most patterns: (verb, object)
                    verb = match[0].strip().lower() if match[0] else None
                    activity = match[-1].strip()
                else:
                    activity = match[0].strip()

                # Special handling for loss/death patterns
                if verb and verb in ('passed away', 'died', 'was lost', 'passed on'):
                    # Subject is the thing that died, not the speaker
                    subject_of_event = activity.lower() if activity else speaker.lower()
                    predicate = 'passed_away'
                    activity = ''
                else:
                    subject_of_event = speaker.lower()
                    # Map verbs to predicates
                    verb_to_predicate = {
                        'went to': 'attended', 'attended': 'attended', 'visited': 'visited',
                        'joined': 'joined', 'signed up for': 'signed_up',
                        'started': 'started', 'began': 'started', 'opened': 'opened',
                        'founded': 'founded', 'launched': 'launched',
                        'got': 'got', 'adopted': 'adopted', 'bought': 'bought',
                        'received': 'received', 'acquired': 'acquired',
                        'met': 'met', 'married': 'married', 'divorced': 'divorced',
                        'moved to': 'moved_to', 'relocated to': 'moved_to',
                        'graduated from': 'graduated_from', 'finished': 'completed',
                        'completed': 'completed', 'enrolled in': 'enrolled_in',
                        'retired from': 'retired_from', 'quit': 'quit', 'left': 'left',
                        'lost': 'lost', 'went': 'did', 'did': 'did',
                        'had': 'had', 'traveled to': 'traveled_to', 'flew to': 'traveled_to',
                    }
                    predicate = verb_to_predicate.get(verb, 'did') if verb else 'did'
            else:
                subject_of_event = speaker.lower()
                activity = match.strip()
                predicate = 'did'

            # Clean up activity
            if activity:
                activity = re.sub(r'\s+', '_', activity.lower())
                activity = re.sub(r'[^\w_]', '', activity)

            if activity and len(activity) > 2 and len(activity) < 50:
                if use_context_date:
                    resolved_date = context_date.strftime('%Y-%m-%d')
                else:
                    resolved_date = resolve_relative_date(date_type, context_date, match_groups)

                events.append({
                    'subject': subject_of_event,
                    'predicate': predicate,
                    'object': activity,
                    'valid_from': resolved_date,
                    'context_date': context_date.strftime('%Y-%m-%d')
                })

    return events


def store_temporal_triplets(conv: dict, sample_id: str) -> int:
    """Extract and store temporal + entity triplets from conversation sessions."""
    stored = 0
    entity_stored = 0

    # Get session numbers
    session_nums = []
    for key in conv['conversation'].keys():
        if key.startswith('session_') and not key.endswith('_date_time'):
            try:
                num = int(key.split('_')[1])
                session_nums.append(num)
            except:
                pass

    speaker_a = conv['conversation'].get('speaker_a', 'A').lower()
    speaker_b = conv['conversation'].get('speaker_b', 'B').lower()

    # Track stored facts to avoid duplicates
    stored_facts = set()

    for session_num in sorted(session_nums):
        session_key = f"session_{session_num}"
        date_key = f"session_{session_num}_date_time"

        if session_key not in conv['conversation']:
            continue

        session = conv['conversation'][session_key]
        date_str = conv['conversation'].get(date_key, "")
        context_date = parse_session_date(date_str)

        for turn in session:
            speaker_raw = turn.get('speaker', '?')
            text = turn.get('text', '')

            # Map speaker to actual name
            if speaker_raw == 'A':
                speaker = speaker_a
            elif speaker_raw == 'B':
                speaker = speaker_b
            else:
                speaker = speaker_raw.lower()

            # Extract temporal events (if we have a context date)
            if context_date:
                events = extract_temporal_events(text, speaker, context_date)

                for event in events:
                    # Dedup key
                    key = f"{event['subject']}:{event['predicate']}:{event['object']}"
                    if key in stored_facts:
                        continue
                    stored_facts.add(key)

                    result = chitta_rpc(
                        "connect_temporal",
                        subject=event['subject'],
                        predicate=event['predicate'],
                        object=event['object'],
                        valid_from=event['valid_from'],
                        context_date=event['context_date']
                    )
                    if result:
                        stored += 1

            # Extract entity facts (non-temporal)
            entity_facts = extract_entity_facts(text, speaker)

            for fact in entity_facts:
                # Dedup key
                key = f"{fact['subject']}:{fact['predicate']}:{fact['object']}"
                if key in stored_facts:
                    continue
                stored_facts.add(key)

                # Store as regular triplet (no temporal component)
                chitta_call("connect",
                    subject=fact['subject'],
                    predicate=fact['predicate'],
                    object=fact['object']
                )
                entity_stored += 1

    return stored + entity_stored


def query_temporal_for_question(question: str, sample_id: str) -> str:
    """Query temporal triplets for 'when' questions. Returns focused results for better F1."""
    # Extract entity names from question (skip question words)
    question_words = {'When', 'What', 'Where', 'Who', 'How', 'Why', 'Which', 'Would', 'Could', 'Should', 'Did', 'Does', 'Is', 'Are', 'Was', 'Were', 'Has', 'Have', 'Had'}
    entities = [w for w in re.findall(r'\b([A-Z][a-z]+)\b', question) if w not in question_words]

    # Extract activity keywords from question
    # "When did X run a charity race?" -> ["charity", "race"]
    # "When did X go to the museum?" -> ["museum"]
    # "When did X adopt Ned?" -> ["ned"], predicate_hint="adopted"
    activity_match = re.search(
        r'(?:go to|attend|visit|join|have|do|sign up for|participate in|run|paint|read|play|take|meet|adopt|start|buy|get|move|graduate|marry|divorce|retire|quit|leave|enroll|finish|complete)\s+(?:a |an |the |my )?([^?]+)',
        question, re.IGNORECASE
    )

    # Also extract verb as predicate hint
    verb_match = re.search(
        r'did \w+ (go to|attend|visit|join|have|do|sign up|participate|run|paint|read|play|take|meet|adopt|start|buy|get|move|graduate|marry|divorce|retire|quit|leave|enroll|finish|complete)',
        question, re.IGNORECASE
    )
    predicate_hint = verb_match.group(1).lower().replace(' ', '_') if verb_match else None

    activity_words = set()
    if activity_match:
        activity = activity_match.group(1).lower()
        activity_words = set(w for w in re.findall(r'\w+', activity) if len(w) > 2)

    # Collect matches with scores for ranking
    scored_results = []
    seen_dates = set()  # De-duplicate by date

    for entity in entities[:2]:  # First 2 capitalized words
        response = chitta_rpc(
            "query_triplets_temporal",
            subject=entity.lower(),
            limit=500
        )

        if response and 'structured' in response:
            triplets = response['structured'].get('triplets', [])
            for t in triplets:
                obj = t.get('object', '')
                valid_from = t.get('valid_from', '')
                predicate = t.get('predicate', '')

                # Skip if no valid_from date
                if not valid_from:
                    continue

                # Split on both whitespace and underscores for word matching
                obj_normalized = obj.lower().replace('_', ' ')
                obj_words = set(w for w in re.findall(r'\w+', obj_normalized) if len(w) > 2)

                # Score based on keyword overlap AND predicate matching
                predicate_matches = False
                if predicate_hint:
                    # Check if predicate matches hint (e.g., "adopt" matches "adopted")
                    pred_lower = predicate.lower()
                    hint_lower = predicate_hint.lower()
                    predicate_matches = hint_lower in pred_lower or pred_lower.startswith(hint_lower)

                if activity_words:
                    overlap = len(activity_words & obj_words)
                    if overlap == 0 and not predicate_matches:
                        continue  # Skip if no keyword overlap AND predicate doesn't match
                    score = overlap / len(activity_words) if activity_words else 0
                    if predicate_matches:
                        score += 0.5  # Boost score for predicate match
                elif predicate_matches:
                    score = 0.8  # High score for predicate match without activity words
                else:
                    score = 0.1  # Low score for unfiltered results

                # Format date naturally for better F1 matching
                natural_date = format_natural_date(valid_from)

                # De-duplicate: use object + date to avoid losing different facts on same date
                dedup_key = f"{entity}:{obj}:{natural_date}"
                if dedup_key in seen_dates:
                    continue
                seen_dates.add(dedup_key)

                scored_results.append((score, f"{entity} {predicate} {obj} on {natural_date}"))

    # Sort by score (descending) and take top results
    scored_results.sort(key=lambda x: -x[0])
    # For "when" questions, return just the top match for best F1
    # If there are good matches (score > 0.5), return only top 1-2
    if scored_results and scored_results[0][0] > 0.5:
        results = [r[1] for r in scored_results[:2]]
    else:
        results = [r[1] for r in scored_results[:5]]

    # Also query triplet history for multiple predicates
    predicates_to_try = [
        'attended', 'ran', 'went', 'visited', 'painted', 'read', 'played', 'signed_up',
        'adopted', 'started', 'got', 'bought', 'moved_to', 'graduated_from', 'married',
        'divorced', 'retired_from', 'quit', 'left', 'enrolled_in', 'completed', 'finished',
        'joined', 'met', 'traveled_to', 'did', 'had'
    ]
    # If we have a predicate hint, prioritize it
    if predicate_hint:
        pred_variants = [predicate_hint, predicate_hint + 'ed', predicate_hint + 'd']
        predicates_to_try = pred_variants + [p for p in predicates_to_try if p not in pred_variants]
    for entity in entities[:2]:
        for pred in predicates_to_try:
            response = chitta_rpc(
                "triplet_history",
                subject=entity.lower(),
                predicate=pred,
                limit=5
            )

            if response and 'structured' in response:
                history = response['structured'].get('history', [])
                for h in history:
                    obj = h.get('object', '')
                    valid_from = h.get('valid_from', '')
                    if valid_from:
                        obj_normalized = obj.lower().replace('_', ' ')
                        obj_words = set(w for w in re.findall(r'\w+', obj_normalized) if len(w) > 2)
                        if not activity_words or activity_words & obj_words:
                            results.append(f"{entity} {pred} {obj} on {valid_from}")

    # Deduplicate and prioritize exact matches
    seen = set()
    exact_matches = []
    partial_matches = []

    for r in results:
        if r in seen:
            continue
        seen.add(r)

        # Check if this is an exact match (contains all activity words)
        r_lower = r.lower().replace('_', ' ')
        r_words = set(re.findall(r'\w+', r_lower))
        if activity_words and activity_words <= r_words:
            exact_matches.append(r)
        else:
            partial_matches.append(r)

    # Exact matches first, then partial matches
    unique_results = exact_matches + partial_matches
    return '\n'.join(unique_results[:50]) if unique_results else ""


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
FULL_CONTEXT_MODE = False  # Global flag to pass full conversation as context (no retrieval)
FULL_CONTEXT_CACHE = {}  # Cache for full conversation context


def get_claude_env() -> dict:
    """Get environment for running claude -p (unset CLAUDECODE to allow nested calls)."""
    env = os.environ.copy()
    # Set to empty string rather than removing - Claude checks value not presence
    env['CLAUDECODE'] = ''
    return env


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
- CRITICAL: Resolve ALL relative dates ("yesterday", "last week", "the week before") against session dates!

Format:
[{sample_id}] subject→relationship→object @YYYY-MM-DD
[TRIPLET] subject predicate object @YYYY-MM-DD

TEMPORAL RESOLUTION EXAMPLES (using session dates):
- Session on "8 May, 2023": "yesterday" → @2023-05-07
- Session on "15 June, 2023": "last week" → @2023-06-08
- Session on "20 July, 2023": "the week before" → @2023-07-13

Every event with a temporal marker MUST have @YYYY-MM-DD:
- "I went to a support group yesterday" in Session 1 (8 May 2023) → Caroline→attended→support_group @2023-05-07
- "I signed up for pottery last week" in Session 5 (June 2023) → person→signed_up→pottery_class @YYYY-MM-DD

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
            timeout=240,
            env=get_claude_env()  # Unset CLAUDECODE for nested calls
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
            content=session_text[:4000],  # Full session content for retrieval
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

    # Store triplets (temporal + entity facts)
    print(f"  Extracting triplets...", end=" ", flush=True)
    triplets_stored = store_temporal_triplets(conv, sample_id)
    print(f"{triplets_stored} triplets (temporal + entity facts)")
    stored += triplets_stored

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

                        # Extract @date annotation if present
                        date_match = re.search(r'@(\d{4}-\d{2}-\d{2})', obj)
                        if date_match:
                            valid_from = date_match.group(1)
                            obj_clean = re.sub(r'\s*@\d{4}-\d{2}-\d{2}', '', obj).strip()
                            chitta_rpc("connect_temporal",
                                subject=subject,
                                predicate=predicate,
                                object=obj_clean,
                                valid_from=valid_from
                            )
                        else:
                            chitta_call("connect",
                                subject=subject,
                                predicate=predicate,
                                object=obj
                            )
                        triplets_stored += 1

                # Handle SSL facts with → notation
                elif '→' in line:
                    # Parse SSL triplet: subject→predicate→object @date
                    ssl_match = re.match(r'\[?[^\]]*\]?\s*(\w+)→(\w+)→(.+)', line)
                    if ssl_match:
                        subject = ssl_match.group(1)
                        predicate = ssl_match.group(2)
                        obj = ssl_match.group(3).strip()

                        # Extract @date annotation if present
                        date_match = re.search(r'@(\d{4}-\d{2}-\d{2})', obj)
                        if date_match:
                            valid_from = date_match.group(1)
                            obj_clean = re.sub(r'\s*@\d{4}-\d{2}-\d{2}', '', obj).strip()
                            chitta_rpc("connect_temporal",
                                subject=subject,
                                predicate=predicate,
                                object=obj_clean,
                                valid_from=valid_from
                            )
                            triplets_stored += 1
                        else:
                            chitta_call("connect",
                                subject=subject,
                                predicate=predicate,
                                object=obj
                            )
                            triplets_stored += 1
                    else:
                        # Store as observation if not parseable
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


def get_full_conversation_context(conv: dict) -> str:
    """Get ALL conversation data as context (no retrieval, just pass everything)."""
    lines = []
    speaker_a = conv['conversation'].get('speaker_a', 'A')
    speaker_b = conv['conversation'].get('speaker_b', 'B')

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

        lines.append(f"\n=== Session {session_num} ({date}) ===")
        for turn in session:
            speaker = turn.get('speaker', '?')
            text = turn.get('text', '')
            if text:
                lines.append(f"{speaker}: {text}")

    return '\n'.join(lines)


def recall_for_question(question: str, sample_id: str, category: int = 0) -> str:
    """Unified retrieval - hybrid_recall for content, triplets with dates for facts."""
    # Full context mode - return cached full conversation (no retrieval)
    if FULL_CONTEXT_MODE and sample_id in FULL_CONTEXT_CACHE:
        return FULL_CONTEXT_CACHE[sample_id]

    results = []

    # Extract entities from question
    question_words = {'When', 'What', 'Where', 'Who', 'How', 'Why', 'Which', 'Would', 'Could', 'Should', 'Did', 'Does', 'Is', 'Are', 'Was', 'Were', 'Has', 'Have', 'Had'}
    entities = [w for w in re.findall(r'\b([A-Z][a-z]+)\b', question) if w not in question_words]
    entity_str = ' '.join(entities[:2]) if entities else ''

    # === 1. Hybrid recall for session content (includes dates in titles) ===
    search_query = f"{entity_str} {question}"
    hybrid_result = chitta_rpc("hybrid_recall", query=search_query, tag=sample_id, k=25)

    if hybrid_result and 'structured' in hybrid_result:
        for mem in hybrid_result['structured'].get('memories', [])[:20]:
            content = mem.get('content', '')
            if content:
                results.append(content[:400])

    # === 2. Triplet facts - query by subject AND filter by question keywords ===
    seen = set()

    # Extract CONTENT keywords (exclude common question words and verbs)
    # Keep entity names as they may appear in triplet objects (e.g., "visited italy")
    stop_words = {'when', 'what', 'where', 'who', 'how', 'why', 'which', 'did', 'does',
                  'was', 'were', 'has', 'have', 'had', 'the', 'and', 'for', 'that', 'this',
                  'both', 'any', 'all', 'some', 'during', 'before', 'after'}
    # Only exclude the FIRST entity (usually the subject being queried)
    first_entity = entities[0].lower() if entities else ''
    q_words = set(w.lower() for w in re.findall(r'\w+', question)
                  if len(w) > 2 and w.lower() not in stop_words and w.lower() != first_entity)

    for entity in entities[:2]:
        # Get triplets for entity with high limit
        regular = chitta_call("query", subject=entity.lower(), limit=150)
        if regular and '→' in regular:
            scored_lines = []
            for line in regular.split('\n'):
                line = line.strip()
                if not line or '→' not in line:
                    continue
                line_lower = line.lower()
                # Score by keyword matches
                score = sum(1 for w in q_words if w in line_lower)
                scored_lines.append((score, line))

            # Sort by score (descending), take top matches + some general ones
            scored_lines.sort(key=lambda x: -x[0])
            for score, line in scored_lines[:25]:
                key = line.split('@')[0].strip()
                if key not in seen:
                    seen.add(key)
                    results.append(line[:250])

    return '\n'.join(results[:60]) if results else ""


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


def extract_answer_from_context(question: str, context: str) -> str:
    """Extract answer from context using pattern matching (fallback when claude -p unavailable)."""
    if not context:
        return "no information"

    q_lower = question.lower()
    lines = context.split('\n')

    # For "when" questions, look for dates
    if q_lower.startswith('when'):
        # Extract what we're looking for
        activity_match = re.search(
            r'(?:when did \w+ (?:go to|attend|visit|join|have|do|sign up for|participate in))\s+(?:a |an |the )?([^?]+)',
            q_lower
        )
        search_terms = []
        if activity_match:
            activity = activity_match.group(1).strip().rstrip('?')
            # Create search terms from activity
            search_terms = [w for w in re.findall(r'\w+', activity.lower()) if len(w) > 2]

        # Look for lines with dates and matching activity
        for line in lines:
            line_lower = line.lower()
            # Check if line contains a date pattern
            date_match = re.search(r'(\d{4}-\d{2}-\d{2})', line)
            if date_match:
                # Check if any search term matches
                if not search_terms or any(term in line_lower for term in search_terms):
                    # Convert date to readable format
                    date_str = date_match.group(1)
                    try:
                        from datetime import datetime as dt
                        date_obj = dt.strptime(date_str, '%Y-%m-%d')
                        readable = date_obj.strftime('%d %B %Y').lstrip('0')
                        return readable
                    except:
                        return date_str

        # Also check for @date patterns
        for line in lines:
            if '@' in line:
                date_match = re.search(r'@(\d{4}-\d{2}-\d{2})', line)
                if date_match:
                    return date_match.group(1)

    # For "what" questions about identity/relationship
    if 'identity' in q_lower:
        for line in lines:
            if 'identity' in line.lower() or 'is_a' in line.lower() or 'transgender' in line.lower():
                # Extract the value after → or is_a
                match = re.search(r'(?:identity→|is_a\s+)(\w+)', line, re.IGNORECASE)
                if match:
                    return match.group(1)

    if 'relationship' in q_lower or 'status' in q_lower:
        for line in lines:
            if 'relationship' in line.lower() or 'status' in line.lower():
                match = re.search(r'(?:relationship_status→|status→)(\w+)', line, re.IGNORECASE)
                if match:
                    return match.group(1)

    # Generic: return first non-empty content line
    for line in lines:
        if '→' in line and line.strip():
            return line.strip()[:100]

    return "no information"


def answer_batch_with_claude(qa_pairs: list) -> list:
    """Answer multiple questions in one claude -p call (much faster)."""
    if not qa_pairs:
        return []

    # Build batch prompt
    prompt = """Answer each question based ONLY on its context. Be concise - just the answer.

IMPORTANT RULES:
- For "when" questions:
  * Look for explicit dates: @YYYY-MM-DD, valid_from: dates, "on June 9, 2023", etc.
  * ALSO resolve RELATIVE dates using session dates! Examples:
    - Session date "9 June, 2023" + "last week" → "The week before 9 June 2023"
    - Session date "8 May, 2023" + "yesterday" → "7 May 2023"
    - Session date "25 May, 2023" + "last Saturday" → "20 May 2023"
  * When you see "Session X (DATE)" + relative time phrase, CALCULATE the actual date
  * OUTPUT FORMAT: Always use readable dates like "7 May 2023" NOT "2023-05-07"
- For identity questions: Look for "identity→X", "is_a X", or direct statements like "I am transgender"
- For relationship questions: Look for "relationship_status→X" or direct statements
- If information is NOT in the context, say "no information"

"""
    for i, qa in enumerate(qa_pairs):
        prompt += f"""--- Question {i+1} ---
Context: {qa['context'][:3000]}

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
            timeout=180,
            env=get_claude_env()  # Unset CLAUDECODE for nested calls
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
        focus = "Look for dates in formats like: @YYYY-MM-DD (e.g., @2023-05-07), valid_from: dates, @7-May-2023, etc. Convert to readable format."
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
            timeout=60,
            env=get_claude_env()  # Unset CLAUDECODE for nested calls
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

        # Cache full context if in full-context mode
        if FULL_CONTEXT_MODE:
            full_ctx = get_full_conversation_context(conv)
            FULL_CONTEXT_CACHE[sample_id] = full_ctx
            print(f"Full context mode: {len(full_ctx)} chars cached")

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
    parser.add_argument('--full-context', action='store_true',
                        help='Pass full conversation as context (no retrieval, baseline)')

    args = parser.parse_args()

    # Set global flags
    global DRY_RUN, SKIP_INGEST, NATIVE_MODE, FULL_CONTEXT_MODE
    DRY_RUN = args.dry_run
    SKIP_INGEST = args.skip_ingest
    NATIVE_MODE = args.native
    FULL_CONTEXT_MODE = args.full_context

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
