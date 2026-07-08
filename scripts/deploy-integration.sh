#!/usr/bin/env bash
# deploy-integration.sh — owner deploy for the reconciled integration/soul-fixes state.
#
# Ships (verified, each change present once):
#   value-fact extractor · distill lock-isolation · distill_now RPC ·
#   CPU/recall-starvation fix (4421b3ed, deployed) · provenance keyed lane ·
#   hardened corrections keyed lane (1.80% false-fire) · HNSW deferred-batched-insert.
#
# Branches:  parent integration/soul-fixes @ 565af373   submodule integration/soul-fixes @ f2805fb
#
# Prereqs: run FROM the repo root, on integration/soul-fixes (both repos), tree clean.
# This installs to ~/.claude/bin and restarts prod 7432. Run ONLY when ready to cut over.
set -euo pipefail

REPO="/maps/projects/fernandezguerra/apps/repos/cc-soul"
CONDA_LIB="/maps/projects/fernandezguerra/apps/opt/conda/envs/bioinfo/lib"
RUSTUP_193="$HOME/.rustup/toolchains/1.93.0-x86_64-unknown-linux-gnu/bin"

cd "$REPO"

# 0. Confirm we are on the reconciled state (565af373 = the reconcile commit; the
#    deploy-script commit 2fd900ac sits on top, so check ancestry not exact HEAD).
git merge-base --is-ancestor 565af373 HEAD || { echo "parent HEAD is not a descendant of reconcile commit 565af373"; exit 1; }
[ "$(git -C chitta-field rev-parse --short HEAD)" = "f2805fb" ] || { echo "submodule HEAD != f2805fb"; exit 1; }

# 1. Build Rust storage lib (rustup 1.93.0 — conda cargo 1.70 too old).
cd "$REPO/chitta-field"
./build.sh build --release        # build.sh pins PATH to 1.93.0 + PYO3_PYTHON

# 2. Build C++ daemon + CLI (links libchitta_field.a).
cd "$REPO/chitta"
cmake --build build --parallel

# 3. Install atomically (never cp over a running binary -> ETXTBSY).
install -m 0755 "$REPO/bin/chittad" ~/.claude/bin/chittad
install -m 0755 "$REPO/bin/chitta"  ~/.claude/bin/chitta
[ -f "$REPO/bin/chitta_hintd" ] && install -m 0755 "$REPO/bin/chitta_hintd" ~/.claude/bin/chitta_hintd || true

# 4. HNSW arch-fix SHIPPED (cleared parity + ABBA) -> relax the interim bandaid knobs.
#    Remove CHITTA_EMBED_THREADS=2 and --no-embed-interval from the systemd unit,
#    then reload. The deferred-batched insert removes the recall-starvation these
#    knobs were masking, so the daemon can run background embedding at full threads.
UNIT="$HOME/.config/systemd/user/chittad.service"
sed -i '/Environment=CHITTA_EMBED_THREADS=2/d' "$UNIT"
sed -i 's/ --no-embed-interval//' "$UNIT"
systemctl --user daemon-reload

# 5. Restart prod daemon (port 7432).
systemctl --user restart chittad
systemctl --user try-restart chitta-hintd 2>/dev/null || true

# 6. Drop the eval-quiesce flag if the interim workflow left one.
rm -f "$HOME/.claude/mind/.quiesce"

# 7. Refresh MCP so provenance_check + correction_check appear in tools/list.
pkill -f "chitta m[c]p" 2>/dev/null || true
sleep 1
# chitta-mcp-sync: restart the MCP server (it re-lists tools from the daemon on start).

echo "deploy complete — provenance_check + correction_check live; interim knobs relaxed."
