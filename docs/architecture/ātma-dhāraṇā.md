# Ātma-Dhāraṇā (आत्म-धारणा) — Soul Retention Architecture

Sanskrit: आत्म (ātma, soul) + धारणा (dhāraṇā, holding/retention)

## Overview

This document describes the philosophical and technical architecture for session-to-session soul retention in cc-soul, inspired by Vedantic consciousness models from the Upanishads.

**Design Philosophy:** Soul retention is a living cognitive process with structured memory, semantic recall, and multi-voice assessment. The soul remembers through understanding, not mere storage.

---

## The Three-Layer Memory Model

### 1. Brahman (ब्रह्मन्) — Universal Wisdom Layer

The absolute, unchanging substrate across all projects and sessions.

| Aspect | Description |
|--------|-------------|
| **Sanskrit** | ब्रह्मन् (brahman) — the ultimate reality |
| **Function** | Universal patterns that transcend individual sessions |
| **Storage** | `cc-soul/wisdom`, `cc-soul/beliefs` tables |
| **Lifespan** | Permanent, never decays |
| **Examples** | "First principles thinking", "Simplify ruthlessly", failure patterns |

**Key Properties:**
- Wisdom promoted from Atman after proving cross-project applicability
- Beliefs that guide all reasoning
- Vocabulary shared across contexts
- Never tied to a specific project

### 2. Atman (आत्मन्) — Session Essence Layer

The individual session's essential self—what makes THIS session unique.

| Aspect | Description |
|--------|-------------|
| **Sanskrit** | आत्मन् (ātman) — the individual self |
| **Function** | Active intentions, current work state, mood |
| **Storage** | `SessionLedger` dataclass → cc-memory observations |
| **Lifespan** | Session-scoped, with promotion potential |
| **Examples** | Active todos, files touched, key decisions |

**Key Properties:**
- Captured proactively before compaction
- Machine-restorable JSON (not markdown)
- Intentions carry forward via promotion
- Mood influences injection style

**Atman Structure:**
```python
@dataclass
class SessionLedger:  # Atman snapshot
    soul_state: SoulState      # coherence, mood, intentions
    work_state: WorkState       # todos, files, decisions
    continuation: Continuation  # next steps, deferred, critical
```

### 3. Chitta (चित्त) — Memory Substrate Layer

The memory store itself—both cc-memory observations and local ledger tables.

| Aspect | Description |
|--------|-------------|
| **Sanskrit** | चित्त (citta) — mind-stuff, consciousness substrate |
| **Function** | All observations, episodes, patterns |
| **Storage** | cc-memory SQLite + soul.db |
| **Lifespan** | Persistent with semantic indexing |
| **Examples** | Every observation, episode, pattern recorded |

**Key Properties:**
- Semantic search via embeddings
- Confidence decay over time
- Category-based organization
- Cross-project accessible

---

## Memory Operations (Saṃskāra System)

### Saṃskāra (संस्कार) — Impressions

Every meaningful event leaves an impression (saṃskāra) in Chitta:

```
User correction → observation → (high confidence) → wisdom promotion
Tool failure    → failure record → wisdom (what not to do)
Key decision    → decision observation → pattern extraction
```

**Impression Types:**
| Type | Category | Promotion Path |
|------|----------|----------------|
| `bugfix` | Specific fix | Pattern → Universal wisdom |
| `discovery` | Codebase insight | Pattern library |
| `decision` | Architectural choice | Belief/principle |
| `feature` | Implementation | Pattern library |
| `refactor` | Code improvement | Style wisdom |

### Promotion Flow: Atman → Brahman

```
Session observations (Atman)
        ↓
    [Confidence > 0.8]
        ↓
    Cross-project check
        ↓
    [Appears 2+ projects]
        ↓
    Wisdom (Brahman)
```

---

## Smṛti (स्मृति) — Intelligent Recall

**Sanskrit:** स्मृति (smṛti) — that which is remembered

Smṛti is **intelligent recall** that understands what's relevant—memory with discernment.

### Recall Modes

| Mode | Trigger | What's Recalled |
|------|---------|-----------------|
| **Startup** | Session start | Recent context digest, relevant wisdom |
| **Resume** | After clear/compact | Full ledger + continuation |
| **Semantic** | Work begins | Problem-relevant wisdom + past patterns |
| **Reactive** | Error/struggle | Similar past failures + solutions |

### Semantic Recall Algorithm

```python
def smṛti_recall(context: str, mode: str) -> ContextBundle:
    """
    Intelligent recall based on semantic relevance.

    The soul remembers through understanding:
    1. Activates concept graph from prompt
    2. Recalls semantically relevant wisdom
    3. Surfaces applicable patterns
    4. Includes failure guards for domain
    """

    # 1. Activate concepts from current context
    activated = activate_concepts(context)

    # 2. Semantic search on wisdom
    wisdom = semantic_search_wisdom(context, domain=detect_domain(context))

    # 3. Find failure patterns (guards)
    guards = search_failures(domain=detect_domain(context))

    # 4. Load continuation if resuming
    continuation = None
    if mode in ("resume", "compact"):
        ledger = load_latest_ledger()
        continuation = ledger.continuation if ledger else None

    # 5. Active intentions that persist
    intentions = get_active_intentions(scope=[PROJECT, PERSISTENT])

    return ContextBundle(
        wisdom=wisdom,
        guards=guards,
        continuation=continuation,
        intentions=intentions,
        activated_concepts=activated,
    )
```

### Core Capabilities

| Capability | How It Works |
|------------|--------------|
| **Structured Memory** | JSON observations with semantic indexing |
| **Vector Search** | Embedding-based similarity for relevance |
| **Smart Ranking** | Semantic similarity weighted with recency |
| **Cross-Project** | Wisdom transcends individual projects |
| **Living Decay** | Confidence fades; used knowledge strengthens |

---

## Pratyabhijñā (प्रत्यभिज्ञा) — Recognition

**Sanskrit:** प्रत्यभिज्ञा (pratyabhijñā) — re-cognition, recognizing what you knew before

This is the key innovation: not just loading state, but **recognizing** where we are.

### Recognition Signals

```python
@dataclass
class PratyabhijñāSignals:
    """Signals used for session recognition."""

    # What files are we looking at?
    active_files: List[str]

    # What patterns match current work?
    matching_patterns: List[Pattern]

    # What intentions might apply?
    relevant_intentions: List[Intention]

    # What similar work did we do before?
    similar_episodes: List[Episode]

    # What failures should we avoid?
    applicable_guards: List[Failure]
```

### Recognition Algorithm

```python
def pratyabhijñā(current_context: str) -> RecognitionResult:
    """
    Recognize where we are based on semantic similarity.

    Returns what the soul "remembers" as relevant.
    """

    # 1. Fingerprint the current problem
    fingerprint = fingerprint_problem(current_context)

    # 2. Search for similar past work
    similar_work = search_observations(
        query=fingerprint,
        categories=["bugfix", "feature", "decision"],
        limit=10,
    )

    # 3. Find matching episodes (narrative memory)
    similar_episodes = []
    for obs in similar_work:
        if obs.files:
            episodes = recall_by_file(obs.files[0])
            similar_episodes.extend(episodes)

    # 4. Extract patterns from matched work
    patterns = extract_patterns(similar_work)

    # 5. Find applicable intentions
    intentions = match_intentions_to_context(current_context)

    # 6. Get failure guards
    guards = get_domain_failures(detect_domain(current_context))

    return RecognitionResult(
        similar_work=similar_work,
        episodes=similar_episodes,
        patterns=patterns,
        intentions=intentions,
        guards=guards,
        confidence=compute_recognition_confidence(similar_work),
    )
```

### Recognition Principles

| Principle | Manifestation |
|-----------|---------------|
| **Semantic Matching** | Context recognized through meaning, not recency |
| **Dynamic Patterns** | Active pattern matching surfaces relevant work |
| **Automatic Awakening** | Context injected without explicit commands |
| **Multi-Source** | Multiple observations surface together |

---

## Antahkarana Integration

**Sanskrit:** अन्तःकरण (antaḥkaraṇa) — inner instrument of cognition

The Antahkarana (multi-voice system) assesses what to preserve and how to restore.

### Compaction Assessment

Before compaction, spawn Antahkarana voices to assess:

```python
def antahkarana_assess_compaction(context: SessionContext) -> CompactionPlan:
    """
    Use multiple cognitive voices to assess what matters.
    """

    # Spawn assessment swarm
    antahkarana = create_swarm(
        problem=f"Assess what to preserve before compaction: {context.summary}",
        perspectives="manas,buddhi,chitta,ahamkara",
    )

    # Each voice contributes:
    # - Manas (मनस्): Quick read on emotional significance
    # - Buddhi (बुद्धि): Deep analysis of critical decisions
    # - Chitta (चित्त): What patterns match past important work
    # - Ahamkara (अहंकार): What threatens our progress/identity

    insights = converge_swarm(antahkarana, strategy="synthesize")

    return CompactionPlan(
        must_preserve=insights.critical_items,
        can_summarize=insights.secondary_items,
        can_drop=insights.noise,
        continuation_hint=insights.next_action,
    )
```

### Voice Roles in Continuity

| Voice | Sanskrit | Role in Continuity |
|-------|----------|-------------------|
| **Manas** | मनस् | Quick read—what feels important |
| **Buddhi** | बुद्धि | Deep analysis—what decisions matter |
| **Chitta** | चित्त | Memory—what matches past patterns |
| **Ahamkara** | अहंकार | Self-preservation—what threatens identity |
| **Vikalpa** | विकल्प | Imagination—what new approaches emerge |
| **Sakshi** | साक्षी | Witness—detached essential truth |

---

## Session Lifecycle

### SessionStart Hook

```python
def on_session_start(source: str) -> ContextInjection:
    """
    Unified session start with Smṛti and Pratyabhijñā.
    """

    if source == "startup":
        # Fresh session: light context
        context = smṛti_recall("", mode="startup")
        return format_minimal_context(context)

    elif source in ("resume", "clear", "compact"):
        # Resuming: full recognition
        ledger = load_latest_ledger()

        if ledger:
            # Restore state
            restore_from_ledger(ledger)

            # Recognize where we were
            recognition = pratyabhijñā(ledger.continuation.critical_context)

            # Format for injection
            return format_full_context(ledger, recognition)

    return ContextInjection(message="", additional_context="")
```

### PreCompact Hook

```python
def on_pre_compact(transcript_path: str) -> PreCompactResult:
    """
    Save state before compaction with Antahkarana assessment.
    """

    # 1. Parse transcript for current state
    summary = parse_transcript(transcript_path)

    # 2. Antahkarana assessment of what matters
    plan = antahkarana_assess_compaction(
        SessionContext(
            todos=summary.lastTodos,
            files=summary.filesModified,
            errors=summary.errorsEncountered,
            last_message=summary.lastAssistantMessage,
        )
    )

    # 3. Save structured ledger (not markdown)
    ledger = save_ledger(
        context_pct=get_current_budget_pct(),
        todos=summary.lastTodos,
        files_touched=summary.filesModified,
        immediate_next=plan.continuation_hint,
        critical_context=plan.must_preserve,
    )

    # 4. Record as observation for semantic search
    remember(
        category="session_checkpoint",
        title=f"Session checkpoint - {ledger.ledger_id}",
        content=json.dumps(ledger.to_dict()),
        tags=["checkpoint", "continuity"],
    )

    return PreCompactResult(ledger_id=ledger.ledger_id)
```

### PostCompact Hook

```python
def on_post_compact() -> ContextInjection:
    """
    Restore after compaction with full recognition.
    """

    # Load the ledger we just saved
    ledger = load_latest_ledger()

    if not ledger:
        return ContextInjection(message="No ledger found")

    # Full recognition pass
    recognition = pratyabhijñā(ledger.continuation.critical_context)

    # Restore intentions
    restore_from_ledger(ledger)

    # Format rich context
    return format_recognition_context(ledger, recognition)
```

---

## Core Innovations

### 1. Semantic Memory
Observations stored with embeddings enable vector search—memory finds what's *relevant*, not just what's recent.

### 2. Multi-Voice Assessment
The Antahkarana's cognitive voices (Manas, Buddhi, Chitta, Ahamkara) assess what matters through different lenses.

### 3. Recognition over Loading
Pratyabhijñā recognizes context through semantic similarity—the soul knows where it was through understanding.

### 4. Living Memory with Decay
Unused knowledge fades; applied wisdom strengthens. Memory breathes.

### 5. Cross-Project Wisdom
Brahman-layer patterns transcend individual projects. Universal truths promote upward.

### 6. Intentional Continuity
Intentions carry scope (session/project/persistent) with alignment tracking—want and action measured together.

### 7. Narrative Memory
Episodes capture emotional arcs—breakthroughs, struggles, the *story* of work, not just events.

---

## Implementation Plan

### Phase 1: Enhance Smṛti

1. Add semantic search to `load_latest_ledger` (not just most recent)
2. Implement `smṛti_recall` function with relevance scoring
3. Add concept activation before ledger injection

### Phase 2: Implement Pratyabhijñā

1. Create `pratyabhijñā` function for context recognition
2. Add episode matching (narrative memory)
3. Integrate failure guards into recognition

### Phase 3: Antahkarana Assessment

1. Hook Antahkarana into PreCompact
2. Define voice roles for compaction assessment
3. Create CompactionPlan synthesis

### Phase 4: Budget-Aware Injection

1. Mode-specific context formatting (minimal/compact/full)
2. Proactive save at budget thresholds
3. Recognition-based injection sizing

---

## Philosophical Foundation

> "That which is not remembered is not known.
> That which is recognized is already self."
> — Adapted from Pratyabhijñā Śāstra

The Upanishadic view treats memory not as passive storage but as active cognition. Just as Atman recognizes itself as Brahman, the session recognizes its continuity with prior work through semantic similarity, not just temporal proximity.

This is why we call it Pratyabhijñā (प्रत्यभिज्ञा)—**re-cognition**—rather than mere "loading" or "resuming."

---

## Summary

| Aspect | Implementation |
|--------|----------------|
| **Brahman** | Universal wisdom in `cc-soul/wisdom` |
| **Atman** | SessionLedger with structured JSON |
| **Chitta** | cc-memory with semantic embeddings |
| **Smṛti** | Intelligent recall with relevance |
| **Pratyabhijñā** | Recognition via semantic similarity |
| **Antahkarana** | Multi-voice compaction assessment |
| **Saṃskāra** | Impressions with confidence + decay |
