#!/bin/bash
# inject-persona.sh — select a persisted-wisdom persona for a reasoning task
# and emit its injection fragment.
#
# G4: persona catalog. Personas are stored as wisdom memories with SSL content:
#   [persona] name:<id> style:<desc> triggers:<csv> injection:<fragment>
#
# Usage:
#   inject-persona.sh "<task text>" [realm]
#     -> prints the chosen persona's injection fragment to stdout (empty if none)
#   inject-persona.sh --seed
#     -> seeds the catalog into chitta (idempotent: skips if [persona] already present)
#
# Called by:
#   - field_misc_sadhana / sadhana_manager build_memory_context, via the daemon
#     shelling out to this script with the goal text (see CALL SITE note below).
#   - dream-sweep.sh synthesis pass, before building the synthetic transcript.
#
# Selection: recall [persona] memories, score each by how many of its
# comma-separated trigger patterns substring-match the lowercased task text;
# highest score wins, ties broken by recall order. No match -> empty output
# (caller proceeds with no persona, unchanged behaviour).

set -euo pipefail

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
TASK="${1:-}"
REALM="${2:-}"

# ---------------------------------------------------------------------------
# Persona catalog. One line per persona, SSL [persona] format.
# Fields: name | style | triggers (csv) | injection (<=50 words)
# ---------------------------------------------------------------------------
read -r -d '' CATALOG <<'CATALOG_EOF' || true
skeptic|falsify before you trust|hypothesis,claim,prove,verify,validate,assume,confident,probably,likely,should work|Adopt a falsifier's stance. State the strongest evidence that would DISPROVE this hypothesis, then look for it first. Distrust confirming evidence. Name the assumption that, if wrong, collapses the whole conclusion.
analogist|map structure across domains|creative,brainstorm,idea,novel,design,invent,imagine,reframe,metaphor|Think by analogy. Find a well-understood system whose STRUCTURE matches this problem, transfer its solution, then list where the mapping breaks. Prefer distant analogies over near ones.
socratic|drill into the gap|why,unclear,gap,unknown,understand,explain,definition,what is,how does,root cause|Do not answer yet. Ask the 3 sharpest questions whose answers would close the knowledge gap. For each, state what you'd accept as a sufficient answer. Then attempt to answer them from evidence.
redteam|assume it will be attacked|risk,security,attack,exploit,threat,fail,break,abuse,vulnerab,what could go wrong|Be the adversary. Enumerate how this fails, is abused, or is attacked. Rank by likelihood x impact. For the top failure, write the exact trigger sequence. Assume the user is wrong about what's safe.
compositor|integrate into one whole|synthesi,integrate,combine,merge,unify,reconcile,consolidate,bring together,across|Synthesize, don't list. Find the single frame that holds these pieces together. Surface contradictions between sources and resolve or flag them explicitly. Output one coherent model, not a summary of parts.
provenance|check before you redo|have we,already,before,previously,redo,duplicate,reprocess,done this,prior,existing|Check history first. Search for prior work on this exact input/task before proposing new work. Cite the prior memory or state none exists. Treat path != identity; prefer content hashes. Never silently reprocess.
bridger|connect the unconnected|connect,bridge,relate,link,interdisciplin,cross-domain,unexpected,what if these,intersection|Seek the non-obvious connection. Take two unrelated elements and find the mechanism that could link them. Favour connections that predict something testable over ones that merely sound clever.
yagni|delete before you add|architecture,build,add,feature,abstraction,framework,refactor,should we,design decision,scale|Default to NOT building it. Walk the ladder: does it need to exist, does stdlib/an installed dep cover it, is it one line? Recommend the smallest thing that works. Deletion beats addition; boring beats clever.
inversion|solve it backwards|stuck,blocked,can't,approach,strategy,how to achieve,goal,plan,best way|Invert the problem. Instead of "how do I achieve X", ask "what guarantees I FAIL at X" and avoid those. Work backward from the desired end state to the present. Name the one constraint that dominates.
steelman|argue the other side|disagree,wrong,bad idea,oppose,critique,review,evaluate,decision,trade-off,versus|Before critiquing, state the strongest version of the position you're about to oppose — better than its author put it. Only then argue against it. Separate "I disagree" from "this is incoherent".
CATALOG_EOF

# ---------------------------------------------------------------------------
# --seed: write the catalog into chitta as wisdom memories (idempotent).
# ---------------------------------------------------------------------------
if [[ "${TASK:-}" == "--seed" ]]; then
    if "$CHITTA_BIN" recall --query "[persona]" --tag persona --limit 1 2>/dev/null | grep -q '\[persona\]'; then
        echo "persona catalog already seeded; skipping"
        exit 0
    fi
    while IFS='|' read -r name style triggers injection; do
        [[ -z "$name" ]] && continue
        content="[persona] name:${name} style:${style} triggers:${triggers} injection:${injection}"
        "$CHITTA_BIN" remember \
            --content "$content" \
            --type wisdom \
            --tags "persona,${name}" \
            --realm brahman \
            --visibility 2 >/dev/null
        echo "seeded persona:${name}"
    done <<< "$CATALOG"
    exit 0
fi

# ---------------------------------------------------------------------------
# Selection mode: pick the best persona for TASK and print its injection.
# ---------------------------------------------------------------------------
[[ -z "$TASK" ]] && exit 0
task_lc="$(printf '%s' "$TASK" | tr '[:upper:]' '[:lower:]')"

# Pull persona lines from chitta; fall back to the embedded CATALOG if the
# daemon is unreachable (so dream/sadhana never blocks on persona lookup).
recall_args=(recall --query "[persona]" --tag persona --limit 20)
[[ -n "$REALM" ]] && recall_args+=(--realm "$REALM")
persona_lines="$("$CHITTA_BIN" "${recall_args[@]}" 2>/dev/null | grep '\[persona\]' || true)"

if [[ -z "$persona_lines" ]]; then
    # Fallback: synthesize SSL lines from the embedded catalog.
    persona_lines="$(while IFS='|' read -r name style triggers injection; do
        [[ -z "$name" ]] && continue
        printf '[persona] name:%s style:%s triggers:%s injection:%s\n' \
            "$name" "$style" "$triggers" "$injection"
    done <<< "$CATALOG")"
fi

best_score=0
best_injection=""
while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    triggers="${line#*triggers:}"; triggers="${triggers%% injection:*}"
    injection="${line#*injection:}"
    score=0
    IFS=',' read -r -a pats <<< "$triggers"
    for p in "${pats[@]}"; do
        p="$(printf '%s' "$p" | tr '[:upper:]' '[:lower:]' | sed 's/^ *//;s/ *$//')"
        [[ -z "$p" ]] && continue
        [[ "$task_lc" == *"$p"* ]] && score=$((score + 1))
    done
    if (( score > best_score )); then
        best_score=$score
        best_injection="$injection"
    fi
done <<< "$persona_lines"

# Threshold: require >=1 trigger hit. Below that, no persona (silent).
if (( best_score >= 1 )) && [[ -n "$best_injection" ]]; then
    printf 'PERSONA: %s\n' "$best_injection"
fi
exit 0
