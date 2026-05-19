#!/bin/bash
# cec-status — one-shot CEC state summary
# Usage: cec-status [--json]

CHITTA_BIN="${CHITTA_BIN:-$HOME/.claude/bin/chitta}"
JSON_MODE=0
[[ "${1:-}" == "--json" ]] && JSON_MODE=1

_run() { timeout 4 "$CHITTA_BIN" "$@" 2>/dev/null; }

# Gather all CEC organ states in parallel via temp files
_tmp=$(mktemp -d)
_run recall_failure_pattern --k 5 --json >"$_tmp/fail" 2>/dev/null &
_run hypothesis_probes      --k 5 --json >"$_tmp/hyp"  2>/dev/null &
_run refutation_stats       --k 5 --json >"$_tmp/ref"  2>/dev/null &
_run list_policies               --json >"$_tmp/pol"  2>/dev/null &
_run turiya_status               --json >"$_tmp/tur"  2>/dev/null &
wait
_fail=$(cat "$_tmp/fail" 2>/dev/null)
_hyp=$( cat "$_tmp/hyp"  2>/dev/null)
_ref=$( cat "$_tmp/ref"  2>/dev/null)
_pol=$( cat "$_tmp/pol"  2>/dev/null)
_tur=$( cat "$_tmp/tur"  2>/dev/null)
rm -rf "$_tmp"

# Parse key metrics
fail_count=$(echo "$_fail" | python3 -c "import json,sys; d=json.load(sys.stdin); print(len(d.get('patterns',[])))" 2>/dev/null || echo 0)
hyp_total=$(echo "$_hyp" | python3 -c "import json,sys; d=json.load(sys.stdin); print(d['result']['total'])" 2>/dev/null || echo 0)
hyp_top=$(echo "$_hyp" | python3 -c "
import json,sys
rows=json.load(sys.stdin)['result']['top_k']
if not rows: print('  (none)'); exit()
for r in rows[:3]:
    print(f\"  rule_{r['rule_id']}: p={r['p_hat']:.2f} probe={r['probe_value']:.3f} [{r['wilson_lower']:.2f},{r['wilson_upper']:.2f}]\")
" 2>/dev/null || echo "  (none)")
ref_live=$(echo "$_ref"   | python3 -c "import json,sys,re; m=re.search(r'live=(\d+)',json.load(sys.stdin).get('stats','')); print(m.group(1) if m else 0)" 2>/dev/null || echo 0)
ref_refuted=$(echo "$_ref" | python3 -c "import json,sys,re; m=re.search(r'refuted=(\d+)',json.load(sys.stdin).get('stats','')); print(m.group(1) if m else 0)" 2>/dev/null || echo 0)
pol_active=$(echo "$_pol" | python3 -c "import json,sys; d=json.load(sys.stdin); print(sum(1 for p in d.get('policies',[]) if p.get('active')))" 2>/dev/null || echo 0)
pol_shadow=$(echo "$_pol" | python3 -c "import json,sys; d=json.load(sys.stdin); print(sum(1 for p in d.get('policies',[]) if not p.get('active')))" 2>/dev/null || echo 0)
tur_diagnosis=$(echo "$_tur" | python3 -c "import json,sys; d=json.load(sys.stdin).get('result',{}); print(d.get('diagnosis','unknown'))" 2>/dev/null || echo "unknown")
tur_trend=$(echo "$_tur"     | python3 -c "import json,sys; d=json.load(sys.stdin).get('result',{}); print(d.get('trend','unknown'))" 2>/dev/null || echo "unknown")
tur_samples=$(echo "$_tur"   | python3 -c "import json,sys; d=json.load(sys.stdin).get('result',{}); print(d.get('samples',0))" 2>/dev/null || echo 0)
tur_details=$(echo "$_tur"   | python3 -c "
import json,sys
d=json.load(sys.stdin).get('result',{})
l=d.get('latest',{})
if not l: print('  (no samples yet)'); exit()
print(f\"  cdawg_states={l.get('cdawg_states',0)}  tape_events={l.get('tape_events',0)}  δstates={l.get('delta_states',0)}\")
print(f\"  q_variance={l.get('q_variance',0):.6f}  mean_probe={l.get('mean_probe_value',0):.4f}\")
print(f\"  tracked_rules={l.get('tracked_rules',0)}  refuted={l.get('refuted_rules',0)}\")
" 2>/dev/null || echo "  (parse error)")

if [[ $JSON_MODE -eq 1 ]]; then
    python3 -c "
import json
print(json.dumps({
    'failure_patterns': $fail_count,
    'hypothesis_market': {'total_rules': $hyp_total},
    'refutation_ledger': {'live': $ref_live, 'refuted': $ref_refuted},
    'intervention_store': {'active': $pol_active, 'shadow': $pol_shadow},
    'turiya': {'diagnosis': '$tur_diagnosis', 'trend': '$tur_trend', 'samples': $tur_samples},
}, indent=2))
"
else
    echo "═══ CEC Status ═══════════════════════════════"
    echo "EventTape / CDAWG"
    echo "  failure patterns:    $fail_count active"
    echo ""
    echo "HypothesisMarket"
    echo "  rules tracked:       $hyp_total"
    echo "  top probes (uncertain):"
    echo "$hyp_top"
    echo ""
    echo "RefutationLedger"
    echo "  live rules:          $ref_live"
    echo "  refuted rules:       $ref_refuted"
    echo ""
    echo "InterventionStore"
    echo "  active policies:     $pol_active"
    echo "  shadow (pending):    $pol_shadow"
    echo ""
    echo "Turīya Monitor (Phase 11)"
    echo "  diagnosis:           $tur_diagnosis"
    echo "  trend:               $tur_trend  (samples: $tur_samples)"
    echo "$tur_details"
    echo "══════════════════════════════════════════════"
fi
