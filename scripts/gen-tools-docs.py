#!/usr/bin/env python3
"""Generate docs/tools.html and docs/API.md from the live chitta daemon.

Source of truth is a `tools/list` call against a running daemon, plus the
visibility split that chitta-mcp/server.py applies (INTERNAL_TOOLS /
ADVANCED_TOOLS are hidden from tools/list but stay callable through the
`advanced` gateway) and the gateway definitions in
chitta-mcp/tools_static.py COMPOSITE_TOOLS.

Stdlib only. Run: python3 scripts/gen-tools-docs.py
"""

import argparse
import ast
import datetime
import html
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(ROOT, "docs")
MCP = os.path.join(ROOT, "chitta-mcp")

# (id, title, matchers) — first match wins. A matcher is an exact tool name or
# a "prefix_" string ending in an underscore.
CATEGORIES = [
    ("core", "Core Memory", [
        "remember", "remember_batch", "remember_typed", "recall", "get", "update",
        "forget", "forget_kind", "tag", "strengthen", "weaken", "expand_memory",
        "pin_memory", "unpin_memory", "list_pinned", "list_memories_brief",
        "list_by_status", "list_by_aspect", "list_aspects", "memory_edit",
        "set_memory_type", "set_priority_tier", "set_affect", "set_evidence_type",
        "get_evidence_type", "promote_memory", "approve_memory", "reject_memory",
        "ack_memory", "nack_memory", "verify_correction", "mark_memory_invalidated",
        "memory_", "labile_memories", "labile_memories_top", "create_episode",
        "prune_episodes", "episode_cluster_status", "what_do_i_know_about",
        "lookup", "list_merge_queue", "resolve_merge", "propose_change",
        "write_gate_stats", "batch_forget", "connect_batch", "get_entities",
    ]),
    ("recall", "Recall & Search", [
        "recall_", "hybrid_recall", "smart_recall", "structured_recall",
        "5w_search", "search_symbols", "query_claims", "expand_query",
        "full_resonate", "resonance_stats", "route_stats", "stageb_set_surface",
        "routed_recall",
    ]),
    ("graph", "Graph & Triplets", [
        "triplets", "triplet_", "query_triplets_temporal", "connect",
        "connect_temporal", "grow", "query", "query_graph", "graph_",
        "cooccurrence_graph", "assoc_", "clear_triplets",
    ]),
    ("code", "Code Intelligence", [
        "find_symbol", "read_symbol", "read_function", "symbol_", "code_context",
        "codebase_overview", "learn_codebase", "embed_symbols", "dedupe_symbols",
        "describe_symbol", "extract_symbols", "type_hierarchy", "resolve_callsites",
        "file_imports", "file_dependents", "clear_codebase", "cleanup_code_wisdom",
    ]),
    ("context", "Context & Status", [
        "soul_context", "smart_context", "compact_context", "trajectory_compact",
        "checkpoint", "observe", "impl_start", "ask", "think_wander", "explore_",
        "soul_repl", "get_policies", "list_policies",
    ]),
    ("realm", "Realms", ["realm_", "remap_realms", "trim_realm_names"]),
    ("session", "Sessions & Continuity", [
        "session_", "ledger_", "long_task_", "task_state", "read_transcript",
        "get_turns", "transcript_",
    ]),
    ("messaging", "Cross-Harness Messaging", [
        "msg_", "cross_harness_conflicts", "session_sync",
    ]),
    ("narrative", "Narrative & Work Modes", ["narrative_", "log_decision", "log_exposure"]),
    ("distill", "Distillation & Embeddings", [
        "distill_", "densify_backfill", "semantic_backfill", "flush_embeddings",
        "get_embeddings", "pending_embed_ids", "embed_", "rebuild_fts_index",
        "compact_wal", "queue_status", "health_check", "chitta_health",
        "health_check_start", "memory_type_stats",
    ]),
    ("consolidation", "Consolidation & Contradictions", [
        "consolidate_similar", "consolidation_", "find_near_duplicates",
        "reconsolidate", "detect_contradictions", "scan_contradictions",
        "resolve_contradiction", "conflict_inspector", "why_active",
        "what_superseded", "show_conflicts", "disable_source", "hygiene_",
        "spectral_drift", "save_spectral_snapshot",
    ]),
    ("theme", "Themes", ["theme_"]),
    ("provenance", "Provenance & Verification", [
        "provenance_check", "correction_check", "calibration_", "probe_",
        "behavioral_probe", "get_relationship_events", "symbol_event_log",
        "span_",
    ]),
    ("sadhana", "Sadhana", ["sadhana", "sadhana_"]),
    ("dream", "Dreams & Curiosity", ["dream_", "curiosity_", "research", "research_"]),
    ("learn", "Learning & Outcomes", [
        "learn", "learn_", "metacognition_", "update_scorer_model",
        "learned_scorer_stats", "effective_scorer_weights",
        "surprise_learning_stats", "record_feedback", "get_source_weights",
        "update_source_weight", "integration_stats", "get_sus_metrics",
    ]),
    ("anticipation", "Anticipation & Habits", [
        "anticipation_", "habit_", "predict_needed", "trigger_",
    ]),
    ("profile", "Profile & Goals", ["profile_", "goal_"]),
    ("skills", "Skill & Agent Registry", [
        "skill_", "agent_", "register_task", "update_task", "add_delegation",
        "link_evidence", "add_probe", "resolve_probe", "set_criterion",
        "get_task", "query_tasks", "agent_protocol_stats",
    ]),
    ("intervention", "Intervention Ledger", [
        "start_intervention", "add_observation", "close_intervention",
        "record_attribution", "query_interventions", "get_intervention",
        "intervention_stats", "list_open_interventions",
    ]),
    ("facts", "Executable Constraints", [
        "assert_fact", "retract_fact", "query_unify", "query_chain",
        "explain_fact", "branch_create", "branch_resolve", "predicate_",
    ]),
    ("surprise", "Surprise & Epistemic Debt", [
        "record_surprise", "query_surprises", "get_blind_spots", "surprise_stats",
        "register_debt", "resolve_debt", "defer_debt", "query_debts",
        "get_fragile_decisions", "debt_stats", "attach_debt_evidence",
    ]),
    ("wisdom", "Wisdom Lifecycle", [
        "upsert_wisdom_candidate", "update_wisdom_lifecycle",
        "query_wisdom_candidates", "wisdom_promotion_stats", "enroll_wisdom_lineage",
        "transition_wisdom_lineage", "close_rederive", "query_wisdom_lineages",
        "get_wisdom_lineage", "wisdom_lineage_stats", "tick_lineage_staleness",
        "lineage_expiry_check", "insight_global", "insight_promote",
    ]),
    ("cec", "Causal Episode Compiler", [
        "log_event", "log_event_ex", "executor_flush", "refutation_stats",
        "hypothesis_probes", "turiya_status", "tape_stats", "verbalize_rules",
        "queue_experiments", "fep_status", "witness_memory", "reconcile_pass",
        "harvest_scope", "seed_hdc_geometry", "epiplexity_check",
    ]),
    ("io", "Import / Export & Files", [
        "ingest_source", "wiki_export", "export_training_pairs", "export_soul",
        "import_soul", "file_", "run_hint_enricher", "sql_query", "cleanup",
        "cycle", "background_", "version_check", "subconscious_stats",
        "suggestion_", "seed_", "advanced",
    ]),
]


def die(msg):
    sys.stderr.write("gen-tools-docs: %s\n" % msg)
    sys.exit(1)


def fetch_live_tools(cli):
    req = '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
    try:
        out = subprocess.run([cli, "mcp"], input=req, capture_output=True,
                             text=True, timeout=60)
    except FileNotFoundError:
        die("chitta CLI not found at %s (pass --chitta)" % cli)
    if out.returncode != 0:
        die("`%s mcp` exited %d: %s" % (cli, out.returncode, out.stderr.strip()[:400]))
    for line in out.stdout.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            doc = json.loads(line)
        except ValueError:
            continue
        if "result" in doc and "tools" in doc["result"]:
            return doc["result"]["tools"]
    die("no tools/list result in daemon output — is chittad running?")


def parse_assigned_set(path, names):
    """ast-parse module-level set literals without importing the module."""
    tree = ast.parse(open(path, encoding="utf-8").read())
    found = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") in names:
            found[node.targets[0].id] = set(ast.literal_eval(node.value))
    missing = set(names) - set(found)
    if missing:
        die("could not parse %s from %s" % (", ".join(sorted(missing)), path))
    return found


def parse_dict_keys(path, name):
    tree = ast.parse(open(path, encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == name:
            return {ast.literal_eval(k) for k in node.value.keys}
    die("%s not found in %s" % (name, path))


def parse_composite_tools(path):
    tree = ast.parse(open(path, encoding="utf-8").read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and getattr(node.targets[0], "id", "") == "COMPOSITE_TOOLS":
            out = []
            for el in node.value.elts:
                kw = {k.arg: k.value for k in el.keywords}
                out.append({
                    "name": ast.literal_eval(kw["name"]),
                    "description": ast.literal_eval(kw["description"]),
                    "inputSchema": ast.literal_eval(kw["inputSchema"]),
                })
            return out
    die("COMPOSITE_TOOLS not found in %s" % path)


def categorize(name):
    for cid, _title, matchers in CATEGORIES:
        for m in matchers:
            if m.endswith("_"):
                if name.startswith(m):
                    return cid
            elif name == m:
                return cid
    return "other"


def params_of(tool):
    schema = tool.get("inputSchema") or {}
    props = schema.get("properties") or {}
    required = set(schema.get("required") or [])
    rows = []
    for pname in sorted(props):
        spec = props[pname] or {}
        ptype = spec.get("type", "any")
        if ptype == "array" and isinstance(spec.get("items"), dict):
            ptype = "array<%s>" % spec["items"].get("type", "any")
        if "enum" in spec:
            ptype = "%s (%s)" % (ptype, "|".join(str(v) for v in spec["enum"]))
        rows.append({
            "name": pname,
            "type": ptype,
            "required": pname in required,
            "default": spec.get("default"),
            "desc": spec.get("description", ""),
        })
    return rows


CAT_ICON = ('<svg viewBox="0 0 20 20" fill="none"><circle cx="10" cy="10" r="7" '
            'stroke="#9DB2D9" stroke-width="1" opacity="0.4"/><circle cx="10" cy="10" '
            'r="3" stroke="#CBAF86" stroke-width="1" opacity="0.5"/></svg>')

NAV_ITEMS = [
    ("getting-started.html", "Get Started"),
    ("philosophy.html", "Philosophy"),
    ("architecture.html", "Architecture"),
    ("recall.html", "Recall"),
    ("benchmarks.html", "Benchmarks"),
    ("chitta-field.html", "chitta-field"),
    ("sadhana.html", "Sadhana"),
    ("context.html", "Context"),
    ("tools.html", "Tools"),
    ("cli.html", "CLI"),
    ("hooks.html", "Hooks"),
    ("skills.html", "Skills"),
    ("changelog.html", "Changelog"),
    ("constellation.html", "Constellation"),
    ("dreams/index.html", "Dreams"),
]


def nav_html(active):
    li = []
    for href, label in NAV_ITEMS:
        cls = ' class="active"' if href == active else ""
        li.append('      <li><a href="%s"%s>%s</a></li>' % (href, cls, label))
    li.append('      <li><a href="https://github.com/genomewalker/chitta" '
              'target="_blank" rel="noopener">GitHub</a></li>')
    return """<nav class="nav">
  <div class="nav-inner">
    <a href="index.html" class="nav-brand">chitta</a>
    <button class="nav-hamburger" onclick="document.querySelector('.nav').classList.toggle('nav-open')" aria-label="Menu">
      <span></span><span></span><span></span>
    </button>
    <ul class="nav-links">
%s
    </ul>
  </div>
</nav>""" % "\n".join(li)


PAGE_STYLE = """<style>
.submenu-toc { position: sticky; top: 60px; z-index: 90;
  background: linear-gradient(180deg, rgba(11,11,16,0.98) 0%, rgba(11,11,16,0.95) 100%);
  backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px);
  border-bottom: 1px solid rgba(122,162,247,0.1); margin-top: -1px; }
.submenu-toc-inner { max-width: 1200px; margin: 0 auto; padding: 0 var(--space-5);
  display: flex; align-items: center; gap: 4px; overflow-x: auto;
  scrollbar-width: none; -ms-overflow-style: none; }
.submenu-toc-inner::-webkit-scrollbar { display: none; }
.submenu-toc-link { display: flex; align-items: center; gap: 8px; padding: 14px 16px;
  color: #565f89; font-family: var(--font-mono); font-size: 0.75rem;
  text-decoration: none; white-space: nowrap; transition: all 0.2s ease; position: relative; }
.submenu-toc-link::before { content: ''; width: 6px; height: 6px; border-radius: 50%;
  background: currentColor; opacity: 0.4; transition: all 0.2s ease; }
.submenu-toc-link:hover, .submenu-toc-link.active { color: var(--sandal-300); }
.submenu-toc-link.active::before { opacity: 1; box-shadow: 0 0 8px currentColor; }
.search-section { padding: var(--space-5) 0 0; }
.search-box { display: flex; align-items: center; gap: var(--space-3);
  border: 1px solid rgba(157,178,217,0.14); border-radius: 6px;
  padding: 10px var(--space-3); background: rgba(157,178,217,0.03); }
.search-input { flex: 1; background: transparent; border: 0; outline: none;
  color: var(--ash-100); font-family: var(--font-mono); font-size: var(--text-sm); }
.search-count { font-family: var(--font-mono); font-size: var(--text-xs); color: #565f89; }
.tool-row.hidden, .tools-category.hidden { display: none; }
.no-results { padding: var(--space-5) 0; color: #565f89; font-family: var(--font-mono);
  font-size: var(--text-sm); }
.tool-flag { font-family: var(--font-mono); font-size: 0.65rem; letter-spacing: 0.04em;
  padding: 1px 6px; border-radius: 3px; margin-left: var(--space-2);
  border: 1px solid rgba(203,175,134,0.3); color: var(--sandal-300); opacity: 0.8; }
.gen-note { font-family: var(--font-mono); font-size: var(--text-xs); color: #565f89;
  margin-top: var(--space-3); }
</style>"""

PAGE_SCRIPT = """<script>
document.querySelectorAll('a[href^="#"]').forEach(function(a) {
  a.addEventListener('click', function(e) {
    var t = document.querySelector(this.getAttribute('href'));
    if (!t) return;
    e.preventDefault();
    window.scrollTo({ top: t.getBoundingClientRect().top + window.pageYOffset - 140, behavior: 'smooth' });
    t.classList.remove('collapsed');
  });
});
(function() {
  var links = document.querySelectorAll('.submenu-toc-link');
  var secs = [];
  links.forEach(function(l) {
    var s = document.getElementById(l.getAttribute('data-section'));
    if (s) secs.push({ el: s, link: l });
  });
  if (!secs.length) return;
  function update() {
    var pos = window.scrollY + 180, active = secs[0];
    secs.forEach(function(s) { if (s.el.offsetTop <= pos) active = s; });
    links.forEach(function(l) { l.classList.remove('active'); });
    active.link.classList.add('active');
  }
  window.addEventListener('scroll', update, { passive: true });
  update();
})();
(function() {
  var rows = document.querySelectorAll('.tool-row');
  rows.forEach(function(row) {
    if (!row.querySelector('.tool-params')) return;
    row.classList.add('expandable');
    var d = row.querySelector('.tool-desc');
    if (d) d.innerHTML += '<span class="tool-expand-icon">&#x25BE;</span>';
    row.addEventListener('click', function() { this.classList.toggle('expanded'); });
  });
})();
(function() {
  var input = document.getElementById('tools-search');
  var count = document.getElementById('search-count');
  var cats = document.querySelectorAll('.tools-category');
  var all = document.querySelectorAll('.tool-row');
  var container = document.querySelector('.tools-section .container');
  if (!input || !all.length) return;
  var total = all.length, noResults = null;
  function filter() {
    var q = input.value.toLowerCase().trim(), visible = 0;
    if (noResults) { noResults.remove(); noResults = null; }
    if (!q) {
      all.forEach(function(t) { t.classList.remove('hidden'); });
      cats.forEach(function(c) { c.classList.remove('hidden'); });
      count.textContent = '';
      return;
    }
    cats.forEach(function(cat) {
      var catName = cat.querySelector('.tools-category-name').textContent.toLowerCase();
      var shown = 0;
      cat.querySelectorAll('.tool-row').forEach(function(t) {
        var n = t.querySelector('.tool-name').textContent.toLowerCase();
        var d = t.querySelector('.tool-desc').textContent.toLowerCase();
        if (n.indexOf(q) !== -1 || d.indexOf(q) !== -1 || catName.indexOf(q) !== -1) {
          t.classList.remove('hidden'); shown++; visible++;
        } else { t.classList.add('hidden'); }
      });
      cat.classList.toggle('hidden', shown === 0);
      if (shown) cat.classList.remove('collapsed');
    });
    count.textContent = visible + ' / ' + total;
    if (!visible) {
      noResults = document.createElement('div');
      noResults.className = 'no-results';
      noResults.textContent = 'No tools match "' + input.value + '"';
      container.appendChild(noResults);
    }
  }
  input.addEventListener('input', filter);
  input.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') { input.value = ''; filter(); input.blur(); }
  });
  document.addEventListener('keydown', function(e) {
    if (e.key === '/' && document.activeElement !== input) { e.preventDefault(); input.focus(); }
  });
})();
</script>"""


def esc(s):
    return html.escape(s or "", quote=True)


def render_params_html(rows):
    if not rows:
        return ""
    out = ['<div class="tool-params"><table class="tool-params-table"><thead><tr>'
           '<th>Parameter</th><th>Type</th><th>Required</th><th>Default</th>'
           '<th>Description</th></tr></thead><tbody>']
    for p in rows:
        default = "&mdash;" if p["default"] is None else esc(json.dumps(p["default"]))
        out.append(
            '<tr><td><span class="param-name">%s</span></td>'
            '<td><span class="param-type">%s</span></td>'
            '<td><span class="%s">%s</span></td>'
            '<td><span class="param-type">%s</span></td>'
            '<td><span class="param-desc">%s</span></td></tr>' % (
                esc(p["name"]), esc(p["type"]),
                "param-required" if p["required"] else "param-optional",
                "Yes" if p["required"] else "No",
                default, esc(p["desc"])))
    out.append("</tbody></table></div>")
    return "".join(out)


def build_html(groups, meta):
    toc = "\n".join(
        '    <a href="#cat-%s" class="submenu-toc-link" data-section="cat-%s">%s</a>'
        % (cid, cid, esc(title)) for cid, title, tools in groups)
    blocks = []
    for cid, title, tools in groups:
        rows = []
        for t in tools:
            flag = ('<span class="tool-flag">gateway</span>' if t["gateway"]
                    else ('<span class="tool-flag">via advanced</span>' if t["hidden"] else ""))
            rows.append(
                '        <div class="tool-row"><span class="tool-name">%s</span>'
                '<span class="tool-desc">%s%s</span>%s</div>' % (
                    esc(t["name"]), esc(t["description"]), flag,
                    render_params_html(t["params"])))
        blocks.append(
            '    <div class="tools-category" id="cat-%s">\n'
            '      <div class="tools-category-header" onclick="this.parentElement.classList.toggle(\'collapsed\')">\n'
            '        <div class="tools-category-icon">%s</div>\n'
            '        <span class="tools-category-name">%s</span>\n'
            '        <span class="tools-category-count">%d tool%s</span>\n'
            '        <span class="tools-category-toggle">&#x25BE;</span>\n'
            '      </div>\n'
            '      <div class="tools-list">\n%s\n      </div>\n'
            '    </div>' % (cid, CAT_ICON, esc(title), len(tools),
                            "" if len(tools) == 1 else "s", "\n".join(rows)))
    title = "%d MCP tools — chitta" % meta["total"]
    desc = ("Generated reference for every MCP tool served by the chitta daemon: "
            "%d listed by default, %d more reachable through the advanced gateway."
            % (meta["visible"], meta["hidden"]))
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>%(title)s</title>
<meta name="description" content="%(desc)s">
<link rel="icon" href="favicon.svg" type="image/svg+xml">
<meta property="og:title" content="%(title)s">
<meta property="og:description" content="%(desc)s">
<meta property="og:type" content="website">
<meta property="og:image" content="favicon.svg">
<meta name="twitter:card" content="summary">
<meta name="twitter:title" content="%(title)s">
<meta name="twitter:description" content="%(desc)s">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Cormorant+Garamond:ital,wght@0,400;0,500;0,600;1,400;1,500&family=IBM+Plex+Mono:ital,wght@0,400;0,500;1,400&display=swap" rel="stylesheet">
<link rel="stylesheet" href="styles.css">
%(style)s
</head>
<body>

%(nav)s

<header class="page-header">
  <div class="container">
    <div class="page-header-badge reveal">MCP tool reference</div>
    <h1 class="reveal reveal-delay-1">%(total)d tools. One daemon.</h1>
    <p class="page-header-sub reveal reveal-delay-2">Every tool is served by <code style="color: var(--aura-300);">chittad</code>, a C++ daemon reached over a Unix socket. %(visible)d are listed to the model by default; the remaining %(hidden)d stay callable through the <code style="color: var(--aura-300);">advanced</code> gateway, which keeps the default tool list small. Click a row for its parameters.</p>
    <p class="gen-note">Generated from a live daemon on %(date)s, %(total)d tools &mdash; regenerate with <code>python3 scripts/gen-tools-docs.py</code></p>
  </div>
</header>

<section class="search-section">
  <div class="container">
    <div class="search-box">
      <input type="text" id="tools-search" class="search-input" placeholder="Filter tools&hellip; (press /)" autocomplete="off" />
      <span class="search-count" id="search-count"></span>
    </div>
  </div>
</section>

<nav class="submenu-toc">
  <div class="submenu-toc-inner">
%(toc)s
  </div>
</nav>

<section class="tools-section">
  <div class="container">

%(blocks)s

  </div>
</section>

<section class="back-section">
  <div class="container">
    <a href="index.html" class="back-link">&larr; Back to chitta</a>
  </div>
</section>

<footer class="footer">
  <div class="container">
    <p class="footer-text">
      Built with conviction. Grounded in philosophy.
      <span class="footer-sep"></span>
      <a href="https://github.com/genomewalker/chitta" target="_blank" rel="noopener">GitHub</a>
    </p>
  </div>
</footer>

%(script)s
</body>
</html>
""" % {
        "title": esc(title), "desc": esc(desc), "style": PAGE_STYLE,
        "nav": nav_html("tools.html"), "toc": toc, "blocks": "\n\n".join(blocks),
        "script": PAGE_SCRIPT, "date": meta["date"], "total": meta["total"],
        "visible": meta["visible"], "hidden": meta["hidden"],
    }


def build_markdown(groups, meta):
    out = [
        "# chitta MCP API reference",
        "",
        "Generated from a live daemon on %s, %d tools — regenerate with "
        "`python3 scripts/gen-tools-docs.py`." % (meta["date"], meta["total"]),
        "",
        "%d tools are listed in `tools/list` by default. The other %d are hidden to keep "
        "the model's tool list small, and stay callable through the `advanced` gateway:"
        % (meta["visible"], meta["hidden"]),
        "",
        "```json",
        '{"tool": "pin_memory", "arguments": {"id": 123}}',
        "```",
        "",
        "`advanced` with `{\"action\": \"list\"}` enumerates them at runtime, optionally "
        "filtered by `category` (`advanced` or `internal`).",
        "",
        "Tools marked **gateway** are composed in `chitta-mcp/server.py` rather than served "
        "directly by the daemon; tools marked **via advanced** are daemon tools kept out of "
        "the default listing.",
        "",
        "## Contents",
        "",
    ]
    for cid, title, tools in groups:
        out.append("- [%s](#%s) — %d" % (title, cid, len(tools)))
    out.append("")
    for cid, title, tools in groups:
        out.append('<a id="%s"></a>' % cid)
        out.append("")
        out.append("## %s" % title)
        out.append("")
        for t in tools:
            flag = " *(gateway)*" if t["gateway"] else (" *(via advanced)*" if t["hidden"] else "")
            out.append("### `%s`%s" % (t["name"], flag))
            out.append("")
            out.append(t["description"] or "_No description._")
            out.append("")
            if t["params"]:
                out.append("| Parameter | Type | Required | Default | Description |")
                out.append("|---|---|---|---|---|")
                for p in t["params"]:
                    default = "—" if p["default"] is None else "`%s`" % json.dumps(p["default"])
                    desc = (p["desc"] or "").replace("|", "\\|")
                    out.append("| `%s` | %s | %s | %s | %s |" % (
                        p["name"], p["type"], "yes" if p["required"] else "no", default, desc))
            else:
                out.append("No parameters.")
            out.append("")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--chitta", default=os.path.expanduser("~/.claude/bin/chitta"))
    ap.add_argument("--out-html", default=os.path.join(DOCS, "tools.html"))
    ap.add_argument("--out-md", default=os.path.join(DOCS, "API.md"))
    args = ap.parse_args()

    live = fetch_live_tools(args.chitta)
    sets = parse_assigned_set(os.path.join(MCP, "server.py"),
                              ["INTERNAL_TOOLS", "ADVANCED_TOOLS"])
    hidden = sets["INTERNAL_TOOLS"] | sets["ADVANCED_TOOLS"]
    composites = parse_composite_tools(os.path.join(MCP, "tools_static.py"))
    composite_names = {c["name"] for c in composites}
    # Tools the MCP server intercepts before the daemon sees them. Some of these
    # keep the daemon's published schema, so flag them explicitly.
    routed = parse_dict_keys(os.path.join(MCP, "server.py"), "COMPOSITE_HANDLERS")
    routed_only = routed - composite_names

    def note(name, desc):
        if name in routed_only:
            return (desc + " Routed by the chitta-mcp gateway before it reaches the "
                    "daemon, so the effective arguments can differ from the daemon "
                    "schema below.")
        return desc

    merged = {}
    for t in live:
        merged[t["name"]] = {
            "name": t["name"],
            "description": note(t["name"], t.get("description", "")),
            "params": params_of(t),
            "gateway": t["name"] in composite_names or t["name"] in routed,
            "hidden": t["name"] in hidden,
        }
    for c in composites:
        merged[c["name"]] = {
            "name": c["name"],
            "description": c.get("description", ""),
            "params": params_of(c),
            "gateway": True,
            "hidden": c["name"] in hidden,
        }

    by_cat = {}
    for name in sorted(merged):
        by_cat.setdefault(categorize(name), []).append(merged[name])
    groups = [(cid, title, by_cat[cid]) for cid, title, _ in CATEGORIES if cid in by_cat]
    if "other" in by_cat:
        groups.append(("other", "Other", by_cat["other"]))

    meta = {
        "total": len(merged),
        "visible": sum(1 for t in merged.values() if not t["hidden"]),
        "hidden": sum(1 for t in merged.values() if t["hidden"]),
        "date": datetime.date.today().isoformat(),
    }

    open(args.out_html, "w", encoding="utf-8").write(build_html(groups, meta))
    open(args.out_md, "w", encoding="utf-8").write(build_markdown(groups, meta))
    sys.stderr.write("wrote %s and %s: %d tools (%d listed, %d via advanced), %d categories\n"
                     % (args.out_html, args.out_md, meta["total"], meta["visible"],
                        meta["hidden"], len(groups)))
    if "other" in by_cat:
        sys.stderr.write("uncategorized (%d): %s\n" % (
            len(by_cat["other"]), ", ".join(t["name"] for t in by_cat["other"])))


if __name__ == "__main__":
    main()
