#!/usr/bin/env python3
"""
Reorganize mcp_tools/ into optimal semantic structure.

Goals:
- Short, clear filenames
- Balanced file sizes (5-15 tools ideal)
- Split oversized files (spanda)
- Merge related small files (self_improvement)
"""

import re
import shutil
from pathlib import Path


def get_tools_dir() -> Path:
    return Path(__file__).parent.parent / "src" / "cc_soul" / "mcp_tools"


# Mapping: old filename -> new filename (None = delete after processing)
RENAMES = {
    "write_operations_growing_the_soul.py": "write.py",
    "read_operations_querying_the_soul.py": "read.py",
    "bridge_operations_soul_project_memory.py": "bridge.py",
    "aspirations_future_direction.py": "aspirations.py",
    "intentions_concrete_wants_that_influence.py": "intentions.py",
    "coherence_integration_measurement.py": "coherence.py",
    "insights_breakthrough_tracking.py": "insights.py",
    "dreams_visions_that_spark_evolution.py": "dreams.py",
    "backup_soul_preservation.py": "backup.py",
    "soul_agent_autonomous_agency.py": "agent.py",
    "temporal_dynamics_time_shapes_memory.py": "temporal.py",
    "curiosity_active_knowledge_gap_detection.py": "curiosity.py",
    "observation_tools_passive_learning.py": "observations.py",
    "narrative_memory_stories_not_just_data.py": "narrative.py",
    "semantic_vector_search.py": "semantic.py",
    "concept_graph.py": "concepts.py",
    "ultrathink_integration.py": "reasoning.py",
    "soul_seeding.py": "seeding.py",
    # These require special handling
    "self_improvement_evolution_insights.py": None,
    "self_improvement_improvement_proposals.py": None,
    "spanda_divine_pulsation_integrated_cycle.py": None,
}


def rename_files(tools_dir: Path) -> None:
    """Rename files to shorter names."""
    for old_name, new_name in RENAMES.items():
        if new_name is None:
            continue
        old_path = tools_dir / old_name
        new_path = tools_dir / new_name
        if old_path.exists():
            shutil.move(old_path, new_path)
            print(f"  {old_name} -> {new_name}")


def merge_evolution_files(tools_dir: Path) -> None:
    """Merge self_improvement_* files into evolution.py."""
    files = [
        tools_dir / "self_improvement_evolution_insights.py",
        tools_dir / "self_improvement_improvement_proposals.py",
    ]

    header = """# =============================================================================
# Evolution - Self-Improvement Through Reflection
# =============================================================================

"""
    content = header

    for f in files:
        if f.exists():
            text = f.read_text()
            # Skip header lines
            lines = text.split("\n")
            start = 0
            for i, line in enumerate(lines):
                if line.startswith("@mcp.tool()"):
                    start = i
                    break
            content += "\n".join(lines[start:]).strip() + "\n\n"
            f.unlink()

    output = tools_dir / "evolution.py"
    output.write_text(content.strip() + "\n")
    print("  Merged self_improvement_* -> evolution.py")


def split_spanda_file(tools_dir: Path) -> None:
    """Split spanda into spanda.py, antahkarana.py, orchestration.py."""
    spanda_file = tools_dir / "spanda_divine_pulsation_integrated_cycle.py"
    if not spanda_file.exists():
        return

    content = spanda_file.read_text()

    # Find function boundaries
    tool_pattern = re.compile(r"@mcp\.tool\(\)\ndef (\w+)\(", re.MULTILINE)
    matches = list(tool_pattern.finditer(content))

    # Group functions by category
    spanda_funcs = ["run_learning_cycle", "run_agency_cycle", "run_evolution_cycle",
                    "run_coherence_feedback", "run_session_start", "run_session_end",
                    "run_daily_maintenance"]

    antahkarana_funcs = ["awaken_antahkarana", "create_swarm", "submit_insight",
                         "submit_swarm_solution", "harmonize_antahkarana", "converge_swarm",
                         "list_antahkaranas", "list_swarms", "get_antahkarana_status",
                         "get_swarm_status"]

    # Rest are orchestration
    orchestration_funcs = ["spawn_real_antahkarana", "spawn_real_swarm",
                           "get_orchestrator_status", "poll_antahkarana_voices",
                           "poll_swarm_agents", "harmonize_real_antahkarana",
                           "converge_real_swarm", "list_antahkarana_insights",
                           "list_swarm_solutions"]

    def extract_functions(func_list: list[str]) -> str:
        """Extract function definitions for given names."""
        result = []
        for i, match in enumerate(matches):
            func_name = match.group(1)
            if func_name in func_list:
                start = match.start()
                # Find end (next function or EOF)
                end = matches[i + 1].start() if i + 1 < len(matches) else len(content)
                result.append(content[start:end].strip())
        return "\n\n\n".join(result)

    # Create spanda.py
    spanda_content = """# =============================================================================
# Spanda - Divine Pulsation (Integrated Cycle Operations)
# =============================================================================

"""
    spanda_content += extract_functions(spanda_funcs) + "\n"
    (tools_dir / "spanda.py").write_text(spanda_content)

    # Create antahkarana.py
    antahkarana_content = """# =============================================================================
# Antahkarana - The Inner Instrument (Multi-Agent Convergence)
#
# In Upanishadic philosophy, Antahkarana is the inner organ of consciousness
# comprising facets: Manas (sensory mind), Buddhi (intellect), Chitta (memory),
# and Ahamkara (ego).
# =============================================================================

"""
    antahkarana_content += extract_functions(antahkarana_funcs) + "\n"
    (tools_dir / "antahkarana.py").write_text(antahkarana_content)

    # Create orchestration.py
    orch_content = """# =============================================================================
# Orchestration - Spawning Real Claude Voices
#
# When the Antahkarana awakens with real voices, each voice becomes a separate
# Claude process. They contemplate independently in Chitta (cc-memory), then
# harmonize their insights.
# =============================================================================

"""
    orch_content += extract_functions(orchestration_funcs) + "\n"
    (tools_dir / "orchestration.py").write_text(orch_content)

    # Remove original
    spanda_file.unlink()
    print("  Split spanda into: spanda.py, antahkarana.py, orchestration.py")


def verify_counts(tools_dir: Path) -> None:
    """Count tools in all files."""
    total = 0
    for f in sorted(tools_dir.glob("*.py")):
        if f.name.startswith("_"):
            continue
        count = f.read_text().count("@mcp.tool()")
        if count > 0:
            print(f"  {f.name}: {count} tools")
            total += count
    print(f"\n  Total: {total} tools")


def main():
    tools_dir = get_tools_dir()

    print("Renaming files...")
    rename_files(tools_dir)

    print("\nMerging evolution files...")
    merge_evolution_files(tools_dir)

    print("\nSplitting spanda file...")
    split_spanda_file(tools_dir)

    print("\nVerifying...")
    verify_counts(tools_dir)


if __name__ == "__main__":
    main()
