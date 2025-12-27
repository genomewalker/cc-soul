# CC-Soul Test Scripts

## test_auto_learning.py

Tests the organic fragment-based learning system.

**Philosophy:**
- Save significant text fragments as raw text
- Claude's understanding provides meaning at read time
- No Python pattern matching for structured extraction (action/what/domain)
- The soul is a mirror, not a parser

**What it tests:**
- Fragment saving and retrieval
- Session summary generation (joined fragments)
- Breakthrough detection (for trigger creation)
- Tension detection (for growth vectors)

**Run:**
```bash
python3 .scripts/test_auto_learning.py
```

**Expected output:** Shows fragments being saved and retrieved, with breakthrough and tension detection still working for semantic triggers.
