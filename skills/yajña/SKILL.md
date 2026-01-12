---
name: yajña
aliases: [coordinate, ritual, parallel, yajna]
description: Coordinated multi-agent execution through Vedic ritual patterns
execution: task
---

# Yajña (यज्ञ)

```ssl
[yajña] multiple hands→one purpose | distinct tasks→shared memory via chitta

roles (ṛtvij):
  hotṛ: research, exploration, information gathering
  adhvaryu: implementation, actual code changes
  udgātṛ: testing, validation, quality assurance
  brahman: main Claude→coordinates, synthesizes

when: plan has parallel tasks | different expertise needed | coordination without blocking
not for: single-focus (use swarm) | simple sequential | tight human feedback

inter-agent communication:
  write: observe(category=signal, tags="thread:<id>,yajña,<role>")
  read: recall_by_tag(tag="thread:<id>")

execution:
  1. narrate(action=start, title="yajña: [goal]")→THREAD_ID
  2. spawn agents parallel (independent) or sequential (dependent)
  3. brahman synthesizes: recall_by_tag→integrate pieces
  4. narrate(action=end, episode_id, emotion={completion|breakthrough|partial})

output:
## Yajña: [Goal]
### Agent Activity
├─ Hotṛ → [finding]
├─ Adhvaryu → [built]
└─ Udgātṛ → [verified]
### Outcome

vs swarm: yajña=coordination of tasks | swarm=debate on one question
```
