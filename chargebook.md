# Chargebook

budget: 100

## Cast Costs

cost_Agent: 10
cost_Task: 8
cost_opencode_start: 20
cost_codex_start: 20
cost_parallel_agents: 15
cost_opencode_discuss: 2
cost_codex_run: 5
cost_codex_discuss: 2
cost_web_search: 3
cost_web_fetch: 2

## Reclaim Events

Charge is reclaimed automatically on durable progress:
- Commit pushed:      +10
- Tests passing:       +5
- Feature complete:   +15
- /checkpoint:        +20
