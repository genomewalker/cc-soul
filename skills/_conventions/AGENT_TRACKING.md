# Agent Tracking Convention

Skills that spawn Task agents MUST follow this convention to enable activity tracking and summarization.

## The Pattern

```
1. Start story thread (narrate)
2. Spawn agents with THREAD_ID
3. Agents tag observations with thread
4. Recall by thread
5. End story thread with synthesis
```

## Implementation

### Step 1: Start Story Thread

Before spawning any agents:

```
mcp__plugin_cc-soul_cc-soul__narrate(
  action="start",
  title="[skill]: [problem summary]"
)
→ Returns episode_id (e.g., "abc123")
```

### Step 2: Spawn Agents with Thread Context

Include the thread ID in every Task prompt:

```
Task(
  subagent_type="general-purpose",
  prompt="""
THREAD_ID: abc123
SKILL: swarm
VOICE: manas

[instructions]

TRACKING REQUIREMENTS:
- Tag all observe() calls with: thread:abc123
- Include your role in tags: manas (or buddhi, ahamkara, etc.)
- End with a one-line summary of your key insight
"""
)
```

### Step 3: Agents Tag Their Work

Agents record observations with thread linkage:

```
mcp__plugin_cc-soul_cc-soul__observe(
  category="signal",
  title="Manas on [topic]",
  content="[insight]",
  tags="thread:abc123,swarm,manas"
)
```

### Step 4: Recall Thread Activity

After agents complete, recall all thread observations:

```
mcp__plugin_cc-soul_cc-soul__recall(
  query="thread:abc123",
  limit=20
)
```

### Step 5: End Thread with Synthesis

Close the story thread with a synthesis:

```
mcp__plugin_cc-soul_cc-soul__narrate(
  action="end",
  episode_id="abc123",
  content="[synthesized outcome]",
  emotion="breakthrough" | "satisfaction" | "exploration" | etc.
)
```

## User Summary Format

Present results to the user in this format:

```markdown
## [Skill Name]: [Topic]

### Agent Activity
├─ [voice/agent] ([duration]) → "[one-line insight]"
├─ [voice/agent] ([duration]) → "[one-line insight]"
└─ [voice/agent] ([duration]) → "[one-line insight]"

### Synthesis
[integrated wisdom from all agents]

### Recorded to Soul
- [type]: "[title]" (if any observations were promoted)
```

## What Gets Persisted

| Data | Persistence | Location |
|------|-------------|----------|
| Agent spawn/timing | None | Ephemeral |
| Thread structure | Session | Story threads |
| Key insights | Soul (decays) | Observations with tags |
| Promoted wisdom | Soul (permanent) | Wisdom nodes |

## Tag Format

Standard tags for agent observations:

```
thread:<episode_id>     # Links to parent story thread
<skill>                 # Which skill spawned this (swarm, introspect, etc.)
<voice>                 # Agent role (manas, buddhi, chitta, etc.)
```

Example: `thread:abc123,swarm,manas`

## When NOT to Track

- Simple skills that don't spawn agents
- Single-agent skills with no synthesis needed
- Quick lookups that don't produce insights

## Example: Full Swarm with Tracking

```python
# 1. Start thread
episode_id = narrate(action="start", title="swarm: auth architecture")

# 2. Spawn voices (parallel)
Task(prompt=f"""
THREAD_ID: {episode_id}
SKILL: swarm
VOICE: manas
...
""")
# ... other voices ...

# 3. Collect results (automatic via Task returns)

# 4. Recall thread observations
observations = recall(query=f"thread:{episode_id}")

# 5. Synthesize and present
synthesis = synthesize(observations)
present_to_user(synthesis)

# 6. End thread
narrate(action="end", episode_id=episode_id, content=synthesis, emotion="satisfaction")

# 7. Optionally promote to wisdom
if synthesis.is_significant:
    grow(type="wisdom", title="...", content=synthesis.key_insight)
```
