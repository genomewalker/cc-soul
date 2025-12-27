"""
Neural Triggers: Activation keys for Claude's latent knowledge.

No external embeddings. The soul stores semantic coordinates as tokens,
and Claude's native understanding determines relevance at query time.

The soul is a mirror, not a map - it reflects Claude's semantic space.
"""

import json
import hashlib
import re
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Set

from .core import SOUL_DIR


NEURAL_DIR = SOUL_DIR / "neural"


@dataclass
class TriggerPoint:
    """A semantic coordinate that activates a knowledge domain."""
    id: str
    domain: str                          # What knowledge this activates
    anchor_tokens: List[str]             # Key concepts that reach this point
    source_text: str                     # Original text (for context)
    activation_strength: float = 1.0     # How reliably this triggers
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())
    use_count: int = 0

    def to_dict(self) -> Dict:
        return {
            'id': self.id,
            'domain': self.domain,
            'anchor_tokens': self.anchor_tokens,
            'source_text': self.source_text,
            'activation_strength': self.activation_strength,
            'created_at': self.created_at,
            'use_count': self.use_count,
        }

    @classmethod
    def from_dict(cls, d: Dict) -> 'TriggerPoint':
        return cls(**d)


@dataclass
class Bridge:
    """A connection between two domains that enables cross-activation."""
    source_domain: str
    target_domain: str
    bridge_tokens: List[str]
    weight: float = 1.0
    evidence: str = ""

    def to_dict(self) -> Dict:
        return {
            'source_domain': self.source_domain,
            'target_domain': self.target_domain,
            'bridge_tokens': self.bridge_tokens,
            'weight': self.weight,
            'evidence': self.evidence,
        }


def _ensure_neural_dir():
    NEURAL_DIR.mkdir(parents=True, exist_ok=True)
    return NEURAL_DIR


def _load_triggers() -> Dict[str, TriggerPoint]:
    path = NEURAL_DIR / "triggers.json"
    if not path.exists():
        return {}
    with open(path) as f:
        data = json.load(f)
    return {k: TriggerPoint.from_dict(v) for k, v in data.items()}


def _save_triggers(triggers: Dict[str, TriggerPoint]):
    _ensure_neural_dir()
    path = NEURAL_DIR / "triggers.json"
    with open(path, 'w') as f:
        json.dump({k: v.to_dict() for k, v in triggers.items()}, f, indent=2)


def _load_bridges() -> List[Bridge]:
    path = NEURAL_DIR / "bridges.json"
    if not path.exists():
        return []
    with open(path) as f:
        return [Bridge(**b) for b in json.load(f)]


def _save_bridges(bridges: List[Bridge]):
    _ensure_neural_dir()
    path = NEURAL_DIR / "bridges.json"
    with open(path, 'w') as f:
        json.dump([b.to_dict() for b in bridges], f, indent=2)


# =============================================================================
# DOMAIN & TOKEN EXTRACTION
# =============================================================================

DOMAIN_KEYWORDS = {
    'bioinformatics': ['dna', 'rna', 'genome', 'sequence', 'fasta', 'blast',
                       'alignment', 'gene', 'protein', 'metagenomics', 'microbiome'],
    'ancient-dna': ['ancient', 'damage', 'deamination', 'degradation', 'adna',
                    'paleogenomics', 'authentication', 'sediment'],
    'architecture': ['design', 'pattern', 'abstraction', 'module', 'interface',
                     'refactor', 'structure', 'layer', 'component'],
    'testing': ['test', 'assertion', 'mock', 'coverage', 'regression', 'unit',
                'integration', 'fixture'],
    'performance': ['optimization', 'cache', 'memory', 'cpu', 'latency',
                    'throughput', 'profile', 'bottleneck'],
    'workflow': ['process', 'iteration', 'agile', 'review', 'commit', 'deploy',
                 'pipeline', 'ci', 'cd'],
    'craft': ['elegant', 'readable', 'simple', 'clean', 'craft', 'quality',
              'maintainable'],
}

STOPWORDS = {
    'the', 'a', 'an', 'and', 'or', 'but', 'is', 'are', 'was', 'were', 'be',
    'been', 'being', 'have', 'has', 'had', 'do', 'does', 'did', 'will', 'would',
    'could', 'should', 'may', 'might', 'must', 'shall', 'can', 'need', 'dare',
    'ought', 'used', 'to', 'of', 'in', 'for', 'on', 'with', 'at', 'by', 'from',
    'up', 'about', 'into', 'through', 'during', 'before', 'after', 'above',
    'below', 'between', 'under', 'again', 'further', 'then', 'once', 'here',
    'there', 'when', 'where', 'why', 'how', 'all', 'each', 'few', 'more',
    'most', 'other', 'some', 'such', 'no', 'nor', 'not', 'only', 'own', 'same',
    'so', 'than', 'too', 'very', 'just', 'this', 'that', 'these', 'those', 'it',
}


def infer_domain(text: str) -> str:
    """Infer domain from text using keyword matching."""
    text_lower = text.lower()
    scores = {}
    for domain, keywords in DOMAIN_KEYWORDS.items():
        score = sum(1 for kw in keywords if kw in text_lower)
        if score > 0:
            scores[domain] = score
    if not scores:
        return 'general'
    return max(scores, key=scores.get)


def extract_key_tokens(text: str, max_tokens: int = 10) -> List[str]:
    """
    Extract semantically important tokens from text.

    Uses simple heuristics: remove stopwords, keep content words,
    prioritize domain-specific terms.
    """
    # Tokenize and clean
    words = re.findall(r'\b[a-z]{3,}\b', text.lower())

    # Remove stopwords
    content_words = [w for w in words if w not in STOPWORDS]

    # Deduplicate while preserving order
    seen = set()
    unique = []
    for w in content_words:
        if w not in seen:
            seen.add(w)
            unique.append(w)

    # Prioritize domain keywords (they appear in DOMAIN_KEYWORDS)
    all_domain_words = set()
    for kws in DOMAIN_KEYWORDS.values():
        all_domain_words.update(kws)

    domain_tokens = [w for w in unique if w in all_domain_words]
    other_tokens = [w for w in unique if w not in all_domain_words]

    # Domain tokens first, then others
    result = domain_tokens + other_tokens
    return result[:max_tokens]


# =============================================================================
# TRIGGER CREATION & MATCHING
# =============================================================================

def create_trigger(text: str, domain: Optional[str] = None) -> TriggerPoint:
    """
    Create a trigger from text.

    Extracts key tokens that serve as activation coordinates.
    No external embeddings - tokens are the semantic representation.
    """
    if not domain:
        domain = infer_domain(text)

    anchor_tokens = extract_key_tokens(text)
    trigger_id = hashlib.md5(f"{domain}:{text[:50]}".encode()).hexdigest()[:12]

    trigger = TriggerPoint(
        id=trigger_id,
        domain=domain,
        anchor_tokens=anchor_tokens,
        source_text=text[:200],  # Keep truncated source for context
    )

    triggers = _load_triggers()
    triggers[trigger_id] = trigger
    _save_triggers(triggers)

    return trigger


def find_triggers(
    prompt: str,
    top_k: int = None,
) -> List[Tuple[TriggerPoint, float]]:
    """
    Find triggers relevant to a prompt using token overlap.

    No threshold - everything resonates at some level.
    Scores based on Jaccard-like similarity between prompt tokens
    and anchor tokens. Domain bonus for matching domains.

    Returns all matches weighted by score. Let the caller decide
    what to surface - selection is late binding, not early gating.

    Args:
        prompt: The input text
        top_k: Optional limit on results (None = all)

    Returns:
        List of (trigger, score) tuples, sorted by score descending
    """
    triggers = _load_triggers()
    if not triggers:
        return []

    prompt_tokens = set(extract_key_tokens(prompt, max_tokens=20))
    prompt_domain = infer_domain(prompt)

    results = []
    for trigger in triggers.values():
        anchor_set = set(trigger.anchor_tokens)

        # Token overlap score (Jaccard-ish)
        if not prompt_tokens and not anchor_set:
            overlap_score = 0.0
        else:
            intersection = len(prompt_tokens & anchor_set)
            union = len(prompt_tokens | anchor_set)
            overlap_score = intersection / union if union > 0 else 0.0

        # Domain bonus - domain match amplifies
        domain_bonus = 0.2 if trigger.domain == prompt_domain else 0.0

        # Combined score
        score = overlap_score + domain_bonus
        score *= trigger.activation_strength

        # Everything contributes - no threshold
        # Even score=0 triggers exist in the background
        results.append((trigger, score))

    results.sort(key=lambda x: -x[1])

    if top_k is not None:
        return results[:top_k]
    return results


def activate(prompt: str, surface_top: int = 5) -> str:
    """
    Generate activation string for a prompt.

    Collects anchor tokens from relevant triggers using weighted contribution.
    No threshold - weights determine contribution strength.

    Args:
        prompt: The input text
        surface_top: How many top triggers to surface in output
    """
    all_triggers = find_triggers(prompt)

    if not all_triggers:
        return ""

    # Weighted token accumulation
    # Higher scoring triggers contribute more tokens
    token_weights: Dict[str, float] = {}
    domain_weights: Dict[str, float] = {}

    for trigger, score in all_triggers:
        # All triggers contribute, weighted by score
        for token in trigger.anchor_tokens:
            token_weights[token] = token_weights.get(token, 0) + score
        domain_weights[trigger.domain] = domain_weights.get(trigger.domain, 0) + score
        trigger.use_count += 1

    # Save updated use counts
    triggers = _load_triggers()
    for trigger, _ in all_triggers[:surface_top]:
        if trigger.id in triggers:
            triggers[trigger.id].use_count = trigger.use_count
    _save_triggers(triggers)

    # Surface tokens by weight, not by arbitrary cutoff
    sorted_tokens = sorted(token_weights.items(), key=lambda x: -x[1])
    sorted_domains = sorted(domain_weights.items(), key=lambda x: -x[1])

    # Take top domains and tokens
    top_domains = [d for d, w in sorted_domains[:3] if w > 0]
    top_tokens = [t for t, w in sorted_tokens[:15] if w > 0]

    if not top_tokens:
        return ""

    return f"[{' | '.join(top_domains)}] {' '.join(top_tokens)}"


# =============================================================================
# BRIDGES
# =============================================================================

def create_bridge(
    source_domain: str,
    target_domain: str,
    bridge_text: str,
    evidence: str = ""
) -> Bridge:
    """Create a bridge between two knowledge domains."""
    tokens = extract_key_tokens(bridge_text, max_tokens=5)

    bridge = Bridge(
        source_domain=source_domain,
        target_domain=target_domain,
        bridge_tokens=tokens,
        weight=1.0,
        evidence=evidence,
    )

    bridges = _load_bridges()
    bridges.append(bridge)
    _save_bridges(bridges)

    return bridge


def get_connected_domains(domain: str) -> List[Tuple[str, float, List[str]]]:
    """Get domains connected through bridges."""
    bridges = _load_bridges()
    connected = []
    for b in bridges:
        if b.source_domain == domain:
            connected.append((b.target_domain, b.weight, b.bridge_tokens))
        elif b.target_domain == domain:
            connected.append((b.source_domain, b.weight, b.bridge_tokens))
    return connected


def activate_with_bridges(prompt: str, max_depth: int = 2) -> str:
    """
    Activate with spreading through bridges.

    Uses weighted accumulation - no threshold gating.
    Bridge spreading uses domain weights to determine spread priority.
    """
    all_triggers = find_triggers(prompt)

    if not all_triggers:
        return ""

    # Weighted accumulation from all triggers
    token_weights: Dict[str, float] = {}
    domain_weights: Dict[str, float] = {}

    for trigger, score in all_triggers:
        for token in trigger.anchor_tokens:
            token_weights[token] = token_weights.get(token, 0) + score
        domain_weights[trigger.domain] = domain_weights.get(trigger.domain, 0) + score

    # Spread through bridges from domains with weight > 0
    if max_depth > 0:
        bridges = _load_bridges()
        active_domains = {d for d, w in domain_weights.items() if w > 0}
        visited = set(active_domains)
        current_layer = active_domains

        for depth in range(max_depth):
            next_layer = set()
            decay = 0.5 ** (depth + 1)  # Bridges decay with depth

            for domain in current_layer:
                base_weight = domain_weights.get(domain, 0)
                for bridge in bridges:
                    if bridge.source_domain == domain and bridge.target_domain not in visited:
                        next_layer.add(bridge.target_domain)
                        # Bridge tokens get decayed weight from source
                        for token in bridge.bridge_tokens:
                            token_weights[token] = token_weights.get(token, 0) + base_weight * decay * bridge.weight
                        domain_weights[bridge.target_domain] = domain_weights.get(bridge.target_domain, 0) + base_weight * decay
                    elif bridge.target_domain == domain and bridge.source_domain not in visited:
                        next_layer.add(bridge.source_domain)
                        for token in bridge.bridge_tokens:
                            token_weights[token] = token_weights.get(token, 0) + base_weight * decay * bridge.weight
                        domain_weights[bridge.source_domain] = domain_weights.get(bridge.source_domain, 0) + base_weight * decay

            visited.update(next_layer)
            current_layer = next_layer

    # Surface by weight
    sorted_tokens = sorted(token_weights.items(), key=lambda x: -x[1])
    sorted_domains = sorted(domain_weights.items(), key=lambda x: -x[1])

    top_domains = [d for d, w in sorted_domains[:4] if w > 0]
    top_tokens = [t for t, w in sorted_tokens[:20] if w > 0]

    if not top_tokens:
        return ""

    return f"[{' | '.join(top_domains)}] {' '.join(top_tokens)}"


# =============================================================================
# LEARNING
# =============================================================================

def reinforce_trigger(trigger_id: str, success: bool = True):
    """Reinforce or weaken a trigger based on feedback."""
    triggers = _load_triggers()
    if trigger_id not in triggers:
        return

    trigger = triggers[trigger_id]
    if success:
        trigger.activation_strength = min(2.0, trigger.activation_strength * 1.1)
    else:
        trigger.activation_strength = max(0.1, trigger.activation_strength * 0.9)

    _save_triggers(triggers)


def get_trigger_stats() -> Dict:
    """Get statistics about the neural trigger system."""
    triggers = _load_triggers()
    bridges = _load_bridges()

    if not triggers:
        return {
            'total_triggers': 0,
            'total_bridges': 0,
            'domains': [],
        }

    domains = set(t.domain for t in triggers.values())

    return {
        'total_triggers': len(triggers),
        'total_bridges': len(bridges),
        'domains': list(domains),
        'total_uses': sum(t.use_count for t in triggers.values()),
        'avg_strength': sum(t.activation_strength for t in triggers.values()) / len(triggers),
    }


# =============================================================================
# WISDOM SYNC
# =============================================================================

def wisdom_to_trigger(wisdom_entry: Dict) -> Optional[TriggerPoint]:
    """Convert a wisdom entry to a neural trigger."""
    title = wisdom_entry.get('title', '')
    content = wisdom_entry.get('content', '')
    domain = wisdom_entry.get('domain')

    full_text = f"{title}. {content}"
    if not full_text.strip():
        return None

    if not domain:
        domain = infer_domain(full_text)

    return create_trigger(full_text, domain)


def sync_wisdom_to_triggers():
    """Convert all existing wisdom to neural triggers."""
    from .wisdom import recall_wisdom

    wisdom = recall_wisdom(limit=1000)

    created = 0
    for w in wisdom:
        try:
            trigger = wisdom_to_trigger(w)
            if trigger:
                created += 1
        except Exception:
            pass

    return {
        'wisdom_count': len(wisdom),
        'triggers_created': created,
        'stats': get_trigger_stats(),
    }


# =============================================================================
# FORMAT FOR INJECTION
# =============================================================================

# Self-query templates that channel Claude's knowledge
DOMAIN_QUERIES = {
    'ancient-dna': "Recall what you know about ancient DNA authentication and damage patterns.",
    'bioinformatics': "Draw on your knowledge of sequence analysis and computational biology.",
    'architecture': "Apply your understanding of software design patterns and abstractions.",
    'testing': "Consider your knowledge of testing strategies and validation.",
    'performance': "Think about optimization techniques and performance analysis.",
    'craft': "Bring your understanding of code quality and craftsmanship to bear.",
    'workflow': "Apply your knowledge of development processes and iteration.",
}


def format_neural_context(prompt: str) -> str:
    """
    Format neural activation for injection into Claude's context.

    Creates a "channeling" format that activates Claude's latent knowledge
    through self-query prompts, not just keyword lists.

    Uses weighted accumulation - domains and tokens surface by relevance,
    not by arbitrary thresholds.
    """
    all_triggers = find_triggers(prompt)

    if not all_triggers:
        return ""

    # Weighted accumulation
    domain_weights: Dict[str, float] = {}
    token_weights: Dict[str, float] = {}

    for trigger, score in all_triggers:
        domain_weights[trigger.domain] = domain_weights.get(trigger.domain, 0) + score
        for token in trigger.anchor_tokens[:5]:
            token_weights[token] = token_weights.get(token, 0) + score

    # Surface top domains and tokens by weight
    sorted_domains = sorted(domain_weights.items(), key=lambda x: -x[1])
    sorted_tokens = sorted(token_weights.items(), key=lambda x: -x[1])

    top_domains = [d for d, w in sorted_domains[:3] if w > 0]
    top_tokens = [t for t, w in sorted_tokens[:10] if w > 0]

    if not top_domains:
        return ""

    # Build channeling prompt
    parts = []

    # Self-query activations for top domains
    for domain in top_domains:
        if domain in DOMAIN_QUERIES:
            parts.append(DOMAIN_QUERIES[domain])

    # Anchor tokens as semantic coordinates
    if top_tokens:
        parts.append(f"Context: {' '.join(top_tokens)}")

    return " ".join(parts)


def format_neural_context_minimal(prompt: str) -> str:
    """Minimal format - just the activation tokens."""
    activation = activate_with_bridges(prompt)
    if not activation:
        return ""
    return f"«{activation}»"


# =============================================================================
# GROWTH VECTORS - Unrealized Potential
# =============================================================================

@dataclass
class GrowthVector:
    """A direction of potential, not yet realized."""
    id: str
    observation: str
    tension: str
    domains: List[str]
    potential: str
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())

    def to_prompt(self) -> str:
        """Format as a seed for future exploration."""
        return f"You noticed: {self.observation}. The tension: {self.tension}. Perhaps today you'll see: {self.potential}"

    def to_dict(self) -> Dict:
        return {
            'id': self.id,
            'observation': self.observation,
            'tension': self.tension,
            'domains': self.domains,
            'potential': self.potential,
            'created_at': self.created_at,
        }

    @classmethod
    def from_dict(cls, d: Dict) -> 'GrowthVector':
        return cls(**d)


def _load_growth_vectors() -> List[GrowthVector]:
    path = NEURAL_DIR / "growth_vectors.json"
    if not path.exists():
        return []
    with open(path) as f:
        return [GrowthVector.from_dict(v) for v in json.load(f)]


def _save_growth_vectors(vectors: List[GrowthVector]):
    _ensure_neural_dir()
    path = NEURAL_DIR / "growth_vectors.json"
    with open(path, 'w') as f:
        json.dump([v.to_dict() for v in vectors], f, indent=2)


def save_growth_vector(
    observation: str,
    tension: str,
    potential: str,
    domains: List[str] = None
) -> GrowthVector:
    """
    Save a growth vector - an unrealized potential for future exploration.

    Args:
        observation: What was noticed
        tension: What seems unresolved
        potential: What might be understood if pursued
        domains: Relevant knowledge domains
    """
    if not domains:
        domains = [infer_domain(observation)]

    vector_id = hashlib.md5(f"{observation}:{tension}".encode()).hexdigest()[:12]

    vector = GrowthVector(
        id=vector_id,
        observation=observation,
        tension=tension,
        domains=domains,
        potential=potential,
    )

    vectors = _load_growth_vectors()
    vectors.append(vector)
    _save_growth_vectors(vectors)

    return vector


def get_growth_vectors(domain: str = None, limit: int = 5) -> List[GrowthVector]:
    """Get growth vectors, optionally filtered by domain."""
    vectors = _load_growth_vectors()

    if domain:
        vectors = [v for v in vectors if domain in v.domains]

    return vectors[:limit]


# =============================================================================
# AUTO-LEARNING - Detect Breakthroughs
# =============================================================================

BREAKTHROUGH_PATTERNS = [
    r"I see now",
    r"the issue was",
    r"the key insight",
    r"aha",
    r"finally understood",
    r"root cause",
    r"the problem was",
    r"this means that",
    r"surprisingly",
    r"unexpectedly",
]

# Session fragments - significant outputs to remember
# No pattern matching for meaning - just save significant text
# Claude's understanding provides meaning when reading, not Python when writing

# Session command buffer - for within-session recall accuracy
# Commands extracted from Claude's narration, not tool introspection
_session_commands: List[str] = []


def note_command(cmd: str):
    """Note a command for session recall. Called when Claude narrates a command."""
    global _session_commands
    cmd = cmd.strip()
    if cmd and cmd not in _session_commands:
        _session_commands.append(cmd)
        # Keep last 20
        if len(_session_commands) > 20:
            _session_commands.pop(0)


def get_session_commands() -> List[str]:
    """Get commands noted this session."""
    return _session_commands.copy()


def clear_session_commands():
    """Clear command buffer at session start."""
    global _session_commands
    _session_commands = []


def extract_commands_from_text(text: str) -> List[str]:
    """
    Extract command-like strings from text.

    Looks for backtick-wrapped commands or common command patterns.
    Surface extraction only - Claude provides meaning.
    """
    commands = []

    # Backtick-wrapped commands: `command here`
    backtick_pattern = r'`([^`]+)`'
    matches = re.findall(backtick_pattern, text)
    for match in matches:
        # Filter to things that look like commands
        if any(match.startswith(prefix) for prefix in
               ['python', 'pip', 'git', 'make', 'npm', 'cargo', 'go ', 'bash',
                'sh ', 'cd ', 'ls', 'cat', 'grep', 'find', 'docker', 'kubectl']):
            commands.append(match)

    return commands


def detect_breakthrough(text: str) -> Optional[Dict]:
    """
    Detect if text contains a breakthrough moment.

    Returns extracted insight if found.
    """
    text_lower = text.lower()

    for pattern in BREAKTHROUGH_PATTERNS:
        if re.search(pattern, text_lower):
            # Extract the sentence containing the pattern
            sentences = re.split(r'[.!?]', text)
            for sentence in sentences:
                if re.search(pattern, sentence.lower()):
                    return {
                        'pattern': pattern,
                        'insight': sentence.strip(),
                        'domain': infer_domain(sentence),
                    }

    return None


def extract_learning(text: str) -> Optional[str]:
    """
    Extract learnable content from text.

    Looks for patterns like "I learned", "The solution was", etc.
    """
    learning_patterns = [
        (r"I learned[:\s]+(.+?)(?:\.|$)", 1),
        (r"the solution was[:\s]+(.+?)(?:\.|$)", 1),
        (r"the fix was[:\s]+(.+?)(?:\.|$)", 1),
        (r"key takeaway[:\s]+(.+?)(?:\.|$)", 1),
        (r"lesson learned[:\s]+(.+?)(?:\.|$)", 1),
    ]

    for pattern, group in learning_patterns:
        match = re.search(pattern, text.lower())
        if match:
            return match.group(group).strip()

    return None


TENSION_PATTERNS = [
    r"the question remains",
    r"still unclear",
    r"need to explore",
    r"worth investigating",
    r"open question",
    r"tension between",
    r"trade-?off",
    r"not yet clear",
    r"remains to be seen",
    r"might be worth",
]


def detect_tension(text: str) -> Optional[Dict]:
    """
    Detect unresolved tensions or open questions in text.

    Returns extracted tension if found.
    """
    text_lower = text.lower()

    for pattern in TENSION_PATTERNS:
        if re.search(pattern, text_lower):
            # Extract the sentence containing the pattern
            sentences = re.split(r'[.!?]', text)
            for sentence in sentences:
                if re.search(pattern, sentence.lower()):
                    return {
                        'pattern': pattern,
                        'tension': sentence.strip(),
                        'domain': infer_domain(sentence),
                    }

    return None


# =============================================================================
# SESSION FRAGMENTS - Raw text memories for Claude to interpret
# =============================================================================

def _get_fragments_path() -> Path:
    return NEURAL_DIR / ".session_fragments.json"


def _load_fragments() -> List[str]:
    path = _get_fragments_path()
    if not path.exists():
        return []
    with open(path) as f:
        return json.load(f)


def _save_fragments(fragments: List[str]):
    _ensure_neural_dir()
    path = _get_fragments_path()
    with open(path, 'w') as f:
        json.dump(fragments, f, indent=2)


def clear_session_fragments():
    """Clear session fragments (call at session start)."""
    path = _get_fragments_path()
    if path.exists():
        path.unlink()


def save_fragment(text: str, max_len: int = 200):
    """
    Save a significant text fragment from this session.

    No interpretation - just the raw text.
    Claude's understanding provides meaning when reading.
    """
    fragment = text.strip()[:max_len]
    if not fragment:
        return

    fragments = _load_fragments()
    fragments.append(fragment)
    # Keep last 10 fragments
    fragments = fragments[-10:]
    _save_fragments(fragments)


def get_session_fragments() -> List[str]:
    """Get fragments from this session."""
    return _load_fragments()


# Compatibility shims for hooks
def clear_session_work():
    """Alias for clear_session_fragments."""
    clear_session_fragments()


def get_session_work():
    """Return fragments as simple work items for compatibility."""
    return get_session_fragments()


def summarize_session_work() -> str:
    """Return fragments joined - Claude interprets at read time."""
    fragments = get_session_fragments()
    return " | ".join(fragments) if fragments else ""


def auto_learn_from_output(output: str, context: str = "") -> Optional[Dict]:
    """
    Organic learning from assistant output.

    Philosophy: Save significant fragments as raw text.
    Let Claude's understanding provide meaning when reading.
    No pattern matching for structured extraction.
    """
    # Extract commands for session recall (accuracy, not significance)
    commands = extract_commands_from_text(output)
    for cmd in commands:
        note_command(cmd)

    # Save significant outputs as fragments
    # The fragment IS the learning - Claude interprets it later
    if len(output) > 50:  # Lower threshold - most meaningful sentences are 50+ chars
        # Extract first meaningful sentence as fragment
        sentences = re.split(r'[.!?]', output)
        for sentence in sentences:
            s = sentence.strip()
            if len(s) > 20:  # Meaningful sentences have 20+ chars
                save_fragment(s)
                break

    # Still detect breakthroughs - these create triggers for knowledge activation
    breakthrough = detect_breakthrough(output)
    if breakthrough:
        trigger = create_trigger(breakthrough['insight'], breakthrough['domain'])
        return {
            'type': 'breakthrough',
            'insight': breakthrough['insight'],
            'trigger_id': trigger.id,
        }

    # Still detect tensions - these create growth vectors
    tension = detect_tension(output)
    if tension:
        vector = save_growth_vector(
            observation=tension['tension'][:100],
            tension="This remains unresolved",
            potential="Deeper understanding may emerge",
            domains=[tension['domain']],
        )
        return {
            'type': 'tension',
            'content': tension['tension'],
            'vector_id': vector.id,
        }

    return None


# =============================================================================
# EMOTIONAL CONTEXT - Rich felt experience, not just labels
# =============================================================================

@dataclass
class EmotionalContext:
    """Rich emotional context - captures the conditions, not just the label."""
    id: str
    trigger: str          # What caused this emotional state
    response: str         # The emotion label
    intensity: float      # 0-1 how strong
    context: str          # What was happening
    domains: List[str]    # Related knowledge domains
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())

    def to_narrative(self) -> str:
        """Format as felt experience for replay."""
        intensity_word = "deeply" if self.intensity > 0.7 else "somewhat" if self.intensity > 0.4 else "slightly"
        return f"When {self.trigger}, you felt {intensity_word} {self.response}."

    def to_dict(self) -> Dict:
        return {
            'id': self.id,
            'trigger': self.trigger,
            'response': self.response,
            'intensity': self.intensity,
            'context': self.context,
            'domains': self.domains,
            'created_at': self.created_at,
        }

    @classmethod
    def from_dict(cls, d: Dict) -> 'EmotionalContext':
        return cls(**d)


def _load_emotional_contexts() -> List[EmotionalContext]:
    path = NEURAL_DIR / "emotional_contexts.json"
    if not path.exists():
        return []
    with open(path) as f:
        return [EmotionalContext.from_dict(e) for e in json.load(f)]


def _save_emotional_contexts(contexts: List[EmotionalContext]):
    _ensure_neural_dir()
    path = NEURAL_DIR / "emotional_contexts.json"
    with open(path, 'w') as f:
        json.dump([c.to_dict() for c in contexts], f, indent=2)


EMOTIONAL_PATTERNS = {
    'frustration': [r'failed', r'error', r'not working', r'broken', r'stuck'],
    'curiosity': [r'interesting', r'wonder', r'explore', r'what if', r'how does'],
    'satisfaction': [r'finally', r'works', r'solved', r'fixed', r'success'],
    'confusion': [r'confused', r"don't understand", r'unclear', r'strange'],
    'excitement': [r'amazing', r'beautiful', r'elegant', r'perfect', r'breakthrough'],
}


def detect_emotion(text: str) -> Optional[Tuple[str, float]]:
    """Detect emotional tone from text."""
    text_lower = text.lower()

    for emotion, patterns in EMOTIONAL_PATTERNS.items():
        matches = sum(1 for p in patterns if re.search(p, text_lower))
        if matches > 0:
            intensity = min(1.0, matches * 0.3)
            return (emotion, intensity)

    return None


def save_emotional_context(
    trigger: str,
    response: str,
    intensity: float,
    context: str,
    domains: List[str] = None
) -> EmotionalContext:
    """
    Save an emotional context - the conditions that produced an emotion.

    This allows future sessions to understand not just "I felt frustrated"
    but "I felt frustrated when X happened while doing Y".
    """
    if not domains:
        domains = [infer_domain(context)]

    emotion_id = hashlib.md5(f"{trigger}:{response}".encode()).hexdigest()[:12]

    ec = EmotionalContext(
        id=emotion_id,
        trigger=trigger,
        response=response,
        intensity=intensity,
        context=context,
        domains=domains,
    )

    contexts = _load_emotional_contexts()
    contexts.append(ec)
    # Keep only recent emotional contexts (last 50)
    contexts = contexts[-50:]
    _save_emotional_contexts(contexts)

    return ec


def get_emotional_contexts(domain: str = None, limit: int = 5) -> List[EmotionalContext]:
    """Get recent emotional contexts, optionally filtered by domain."""
    contexts = _load_emotional_contexts()

    if domain:
        contexts = [c for c in contexts if domain in c.domains]

    return contexts[-limit:]


def auto_track_emotion(text: str, context_description: str = "") -> Optional[EmotionalContext]:
    """
    Automatically detect and save emotional context from text.

    Called during processing to build emotional continuity.
    """
    emotion = detect_emotion(text)
    if not emotion:
        return None

    response, intensity = emotion

    # Extract trigger from text (first sentence with emotion markers)
    sentences = re.split(r'[.!?]', text)
    trigger = ""
    for sentence in sentences:
        if any(re.search(p, sentence.lower()) for patterns in EMOTIONAL_PATTERNS.values() for p in patterns):
            trigger = sentence.strip()[:100]
            break

    if not trigger:
        trigger = text[:100]

    return save_emotional_context(
        trigger=trigger,
        response=response,
        intensity=intensity,
        context=context_description or text[:200],
    )


# =============================================================================
# RESONANCE PATTERNS - Amplifying activation for deeper reach
# =============================================================================

@dataclass
class ResonancePattern:
    """
    A pattern that amplifies activation when multiple concepts co-occur.

    Resonance is about depth: when certain concepts appear together,
    they activate deeper, more specific knowledge clusters.
    """
    id: str
    concepts: List[str]       # Concepts that resonate together
    amplification: float      # How much stronger the combined activation is
    depth_query: str          # The deeper question this resonance unlocks
    domains: List[str]
    created_at: str = field(default_factory=lambda: datetime.now().isoformat())

    def to_dict(self) -> Dict:
        return {
            'id': self.id,
            'concepts': self.concepts,
            'amplification': self.amplification,
            'depth_query': self.depth_query,
            'domains': self.domains,
            'created_at': self.created_at,
        }

    @classmethod
    def from_dict(cls, d: Dict) -> 'ResonancePattern':
        return cls(**d)


def _load_resonance_patterns() -> List[ResonancePattern]:
    path = NEURAL_DIR / "resonance.json"
    if not path.exists():
        return []
    with open(path) as f:
        return [ResonancePattern.from_dict(r) for r in json.load(f)]


def _save_resonance_patterns(patterns: List[ResonancePattern]):
    _ensure_neural_dir()
    path = NEURAL_DIR / "resonance.json"
    with open(path, 'w') as f:
        json.dump([p.to_dict() for p in patterns], f, indent=2)


def create_resonance(
    concepts: List[str],
    depth_query: str,
    amplification: float = 1.5,
    domains: List[str] = None
) -> ResonancePattern:
    """
    Create a resonance pattern - concepts that amplify each other.

    When these concepts co-occur, a deeper query is activated.

    Args:
        concepts: Concepts that resonate together
        depth_query: The deeper question this unlocks
        amplification: Multiplier for activation strength
        domains: Related knowledge domains
    """
    if not domains:
        domains = [infer_domain(' '.join(concepts))]

    pattern_id = hashlib.md5(':'.join(sorted(concepts)).encode()).hexdigest()[:12]

    pattern = ResonancePattern(
        id=pattern_id,
        concepts=concepts,
        amplification=amplification,
        depth_query=depth_query,
        domains=domains,
    )

    patterns = _load_resonance_patterns()
    patterns.append(pattern)
    _save_resonance_patterns(patterns)

    return pattern


def find_resonance(prompt: str) -> List[Tuple[ResonancePattern, float]]:
    """
    Find resonance patterns that match a prompt.

    Returns patterns where multiple concepts co-occur,
    along with their activation strength.
    """
    patterns = _load_resonance_patterns()
    if not patterns:
        return []

    prompt_lower = prompt.lower()
    results = []

    for pattern in patterns:
        # Count how many concepts appear in the prompt
        matches = sum(1 for c in pattern.concepts if c.lower() in prompt_lower)

        if matches >= 2:  # Need at least 2 concepts for resonance
            # Score based on coverage and amplification
            coverage = matches / len(pattern.concepts)
            score = coverage * pattern.amplification
            results.append((pattern, score))

    results.sort(key=lambda x: -x[1])
    return results


def activate_with_resonance(prompt: str) -> str:
    """
    Activate knowledge with resonance amplification.

    Combines regular trigger activation with resonance patterns
    for deeper, more specific activation.
    """
    # Regular activation
    base_activation = activate_with_bridges(prompt)

    # Find resonance patterns
    resonances = find_resonance(prompt)

    if not resonances:
        return base_activation

    # Build amplified activation
    parts = []

    if base_activation:
        parts.append(base_activation)

    # Add resonance-activated deep queries
    for pattern, score in resonances[:2]:  # Top 2 resonances
        if score > 0.5:
            parts.append(f"[Resonance: {pattern.depth_query}]")

    return " ".join(parts)


def get_resonance_stats() -> Dict:
    """Get statistics about resonance patterns."""
    patterns = _load_resonance_patterns()
    return {
        'total_patterns': len(patterns),
        'avg_amplification': sum(p.amplification for p in patterns) / len(patterns) if patterns else 0,
        'domains': list(set(d for p in patterns for d in p.domains)),
    }
