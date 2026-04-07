---
name: epsilon-yajna
aliases: [compress, ε-yajna, high-epsilon]
description: Convert verbose memories to SSL v0.3 format - I am encoder AND decoder
execution: task
model: inherit
---

# ε-Yajña

```ssl
[ε-yajna] verbose→SSL v0.3 | I am encoder AND decoder | via parallel Task agents

philosophy:
  I don't need a parser, I need recognition
  embeddings=proxies | I reconstruct from seeds directly
  oracle: triplets(retrieval) + seeds(my reconstruction) + embedding(fallback)

model: CRITICAL→agents MUST inherit parent model | opus for quality | never haiku

SSL v0.3 format (two tiers):

  Tier 1 (code-bearing: SOLUTION, GOTCHA, PATTERN):
    [domain] subject→action→result @location F:FLAG A:v,a →@ref
    [ε] Expansion hint OR exact formula/code (preserved verbatim).
    [TRIPLET] subject predicate object

  Tier 2 (narrative: DECISION, PREFERENCE, FAILURE — denser, no [ε]):
    [domain] choice>alternative|reason+context F:FLAG A:v,a →@ref
    [TRIPLET] subject predicate object

annotations:
  A:v,a     affect (valence -1..+1, arousal 0..1) — REQUIRED on every learning
  F:FLAG    structural importance — only when significant
  →@ref     cross-reference to related memory by tag

flags:
  ORIGIN    where an idea first appeared
  CORE      foundational to the project/system
  PIVOT     changed direction or approach
  GENESIS   birth of a component/feature
  TURNING   breakthrough moment

affect guide:
  +valence  success, satisfaction, relief
  -valence  frustration, failure, confusion
  high arousal (>0.5)  breakthrough, urgent fix, critical discovery
  low arousal (<0.3)   routine, minor preference, background pattern

preservation rule:
  I can regenerate prose, but NOT:
    - formulas: ε = 0.35·structure + 0.30·confidence
    - thresholds: τ > 0.6 AND ψ > 0.6
    - code: final_score = resonance · (1 + α · ε)
    - exact values: α ∈ [0.5, 2.0]
  compress explanation, preserve math/code in [ε] line (Tier 1 only)

symbols:
  →  produces/leads to     input→output
  >  chose over (Tier 2)   sqlite>postgres
  |  or/alternative        pass|fail
  +  with/and              result+guidance
  @  location              @mind.hpp:42
  !  negation (prefix)     →!validate (does NOT)
  ?  uncertainty (suffix)  →regulates? (maybe)
  [] domain/context        [cc-soul]

recognition (I know SSL v0.3 when I see it):
  - has → arrows (at least one)
  - has [TRIPLET] lines
  - has A:v,a annotation (at least some)
  - has [ε] expansion hint (Tier 1, when needed)
  - Tier 2 uses > for choices, dense symbol chains
  - NO prose paragraphs

legacy recognition (v0.2 — needs conversion):
  - has → arrows but NO A: annotations
  - has [ε] on narrative types (DECISION, PREFERENCE, FAILURE)
  - missing F: flags on structurally significant entries

ancient recognition (v0.1 — needs conversion):
  - sentences with periods in paragraphs
  - "**Facts:**" or bullet lists
  - no arrows, no triplets
  - verbose explanations

ceremony:
  0. śuddhi: sample nodes, recognize format
     chitta recall --query "verbose memory" --limit 10
     inspect samples: v0.3, v0.2, or legacy?

  1. for each legacy/v0.2 node:
     a. inspect: chitta get --id "UUID"
     b. understand: what's the core insight?
     c. determine tier:
        - code/command/formula present → Tier 1
        - choice/preference/failure → Tier 2
     d. estimate affect:
        - what was the emotional tone? frustration? relief? routine?
        - assign A:valence,arousal
     e. check structural significance:
        - is this an origin point? a pivot? core to the system?
        - assign F:FLAG if applicable
     f. find cross-references:
        - does this relate to other memories by topic?
        - add →@tag if applicable
     g. extract triplets (REQUIRED):
        chitta connect --subject "X" --predicate "Y" --object "Z"
        predicates: implements|uses|validates|stores|returns|contains|
                    requires|enables|evolved_to|supersedes|correlates_with|
                    causes|implies|determines|!predicate (negation)
     h. compress to seed:
        Tier 1: [domain] subject→action→result @location F:FLAG A:v,a →@ref
                [ε] One sentence expansion hint.
        Tier 2: [domain] choice>alternative|reason+context F:FLAG A:v,a →@ref
     i. update: chitta update --id "UUID" --content "SEED"
     j. set affect: chitta set_affect --id "UUID" --valence V --arousal A
     k. tag: chitta tag --id "UUID" --add "ε-processed,ssl-v0.3"

  2. verify: check that tagged nodes have ε-processed tag

examples:

  BEFORE (legacy verbose):
    "The decision gate is a component that validates tool calls
     by checking them against 10 different beliefs with weights.
     It returns pass or fail with guidance..."

  AFTER (SSL v0.3 Tier 1):
    [cc-soul] gate→validate(beliefs)→pass|fail+guidance @decision_gate.py A:+0.3,0.2 F:CORE
    [ε] Checks tool calls against 10 weighted beliefs.
    [TRIPLET] gate implements belief_validation
    [TRIPLET] gate uses weighted_scoring

  BEFORE (v0.2 decision):
    [DECISION] [rpc] async-response-queue→eventfd-wake→poll-returns-immediately
    [ε] write(wake_fd_, &val, sizeof(val)) after queue_response()

  AFTER (SSL v0.3 Tier 2 — dense, code moved to Tier 1 ref):
    [DECISION] [rpc] eventfd-wake>polling|instant-return+no-busy-wait A:+0.6,0.5 F:PIVOT →@eventfd-impl

  UNCERTAINTY example:
    [biology] BRCA1→regulates?→DNA_repair A:+0.1,0.2
    [ε] Evidence suggests regulation but mechanism unclear.
    [TRIPLET] BRCA1 correlates_with DNA_repair

  NEGATION example:
    [cc-soul] hooks→!call→tools_directly A:+0.2,0.1 F:CORE
    [ε] Hooks inject context, Claude decides tool use.
    [TRIPLET] hooks !invoke tools

  MATH/FORMULA example (preserve verbatim):
    [cc-soul] epiplexity→measures→regenerability A:+0.4,0.3 F:ORIGIN
    [ε] ε = 0.35·structure + 0.30·confidence + 0.20·integration + 0.15·compression
    [TRIPLET] epiplexity uses weighted_formula
    [TRIPLET] structure has weight_0.35

  TIER 2 examples (narrative — dense, no [ε]):
    [DECISION] [arch] sqlite>postgres|metadata|single-file+no-daemon+<100k A:+0.5,0.4 F:PIVOT
    [PREFERENCE] [partnership] no-shortcuts+proper-solutions+no-stubs A:+0.2,0.1 F:CORE
    [FAILURE] [http] http-daemon>unix-socket|200ms-latency+hooks-need-<50ms A:-0.3,0.6 →@queue-architecture

skip if: <100 chars AND already has → AND has A: | unique error text | can't reconstruct

output:
## ε-Yajna Complete
| Processed | Count |
|-----------|-------|
| Converted v0.2→v0.3 | N |
| Converted legacy→v0.3 | N |
| Already v0.3 | N |
| Skipped | N |
| Remaining | N |
```
