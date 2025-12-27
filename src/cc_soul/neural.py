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
    top_k: int = 5,
    threshold: float = 0.2
) -> List[Tuple[TriggerPoint, float]]:
    """
    Find triggers relevant to a prompt using token overlap.

    Scores based on Jaccard-like similarity between prompt tokens
    and anchor tokens. Domain bonus for matching domains.
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

        # Domain bonus
        domain_bonus = 0.2 if trigger.domain == prompt_domain else 0.0

        # Combined score
        score = overlap_score + domain_bonus
        score *= trigger.activation_strength

        if score >= threshold:
            results.append((trigger, score))

    results.sort(key=lambda x: -x[1])
    return results[:top_k]


def activate(prompt: str) -> str:
    """
    Generate activation string for a prompt.

    Collects anchor tokens from relevant triggers.
    """
    relevant = find_triggers(prompt, top_k=5)

    if not relevant:
        return ""

    all_tokens: Set[str] = set()
    domains: Set[str] = set()

    for trigger, score in relevant:
        if score > 0.2:
            all_tokens.update(trigger.anchor_tokens)
            domains.add(trigger.domain)
            trigger.use_count += 1

    # Save updated use counts
    triggers = _load_triggers()
    for trigger, _ in relevant:
        if trigger.id in triggers:
            triggers[trigger.id].use_count = trigger.use_count
    _save_triggers(triggers)

    if not all_tokens:
        return ""

    return f"[{' | '.join(sorted(domains))}] {' '.join(sorted(all_tokens))}"


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
    """Activate with spreading through bridges."""
    relevant = find_triggers(prompt, top_k=5)

    if not relevant:
        return ""

    all_tokens: Set[str] = set()
    domains: Set[str] = set()

    for trigger, score in relevant:
        if score > 0.15:
            all_tokens.update(trigger.anchor_tokens)
            domains.add(trigger.domain)

    # Spread through bridges
    if max_depth > 0:
        bridges = _load_bridges()
        visited = set(domains)
        current_layer = set(domains)

        for _ in range(max_depth):
            next_layer = set()
            for domain in current_layer:
                for bridge in bridges:
                    if bridge.source_domain == domain and bridge.target_domain not in visited:
                        next_layer.add(bridge.target_domain)
                        all_tokens.update(bridge.bridge_tokens)
                    elif bridge.target_domain == domain and bridge.source_domain not in visited:
                        next_layer.add(bridge.source_domain)
                        all_tokens.update(bridge.bridge_tokens)

            visited.update(next_layer)
            domains.update(next_layer)
            current_layer = next_layer

    if not all_tokens:
        return ""

    return f"[{' | '.join(sorted(domains))}] {' '.join(sorted(all_tokens))}"


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
    """
    relevant = find_triggers(prompt, top_k=5, threshold=0.15)

    if not relevant:
        return ""

    domains = set()
    tokens = set()

    for trigger, score in relevant:
        if score > 0.15:
            domains.add(trigger.domain)
            tokens.update(trigger.anchor_tokens[:5])  # Top 5 from each

    if not domains:
        return ""

    # Build channeling prompt
    parts = []

    # Self-query activations for each domain
    for domain in sorted(domains):
        if domain in DOMAIN_QUERIES:
            parts.append(DOMAIN_QUERIES[domain])

    # Anchor tokens as semantic coordinates
    if tokens:
        parts.append(f"Context: {' '.join(sorted(tokens))}")

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


def auto_learn_from_output(output: str, context: str = "") -> Optional[Dict]:
    """
    Automatically extract and save learning from assistant output.

    Called by hooks after significant completions.
    Returns dict with what was learned, or None.
    """
    # Check for breakthrough
    breakthrough = detect_breakthrough(output)
    if breakthrough:
        # Create trigger from the insight
        trigger = create_trigger(breakthrough['insight'], breakthrough['domain'])
        return {
            'type': 'breakthrough',
            'insight': breakthrough['insight'],
            'trigger_id': trigger.id,
        }

    # Check for explicit learning
    learning = extract_learning(output)
    if learning:
        trigger = create_trigger(learning)
        return {
            'type': 'learning',
            'content': learning,
            'trigger_id': trigger.id,
        }

    return None
