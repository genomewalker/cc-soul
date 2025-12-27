"""
Command-line interface for cc-soul.
"""

import sys
import os
import shutil
import argparse
from pathlib import Path

from .core import init_soul, summarize_soul, get_soul_context
from .wisdom import recall_wisdom, get_pending_applications, get_session_wisdom, gain_wisdom, WisdomType
from .beliefs import get_beliefs, hold_belief
from .vocabulary import get_vocabulary, learn_term
from .identity import observe_identity, IdentityAspect
from .hooks import session_start, session_end, user_prompt
from .conversations import save_context, get_saved_context, get_recent_context, format_context_restoration
from .vectors import reindex_all_wisdom
from .evolve import get_evolution_insights, get_evolution_summary, seed_evolution_insights
from .introspect import (
    generate_introspection_report,
    format_introspection_report,
    get_pain_points,
    analyze_pain_points,
    get_wisdom_timeline,
    get_wisdom_health,
    format_wisdom_stats,
    get_session_comparison,
    get_growth_trajectory,
    get_learning_patterns,
    format_trends_report,
    get_decay_visualization,
    format_decay_chart,
)
from .improve import (
    diagnose,
    suggest_improvements,
    format_improvement_prompt,
    get_proposals,
    get_improvement_stats,
    ImprovementStatus,
)
from .ultrathink import (
    enter_ultrathink,
    exit_ultrathink,
    format_ultrathink_context,
    record_discovery,
    commit_session_learnings,
)
from .efficiency import (
    fingerprint_problem,
    learn_problem_pattern,
    add_file_hint,
    get_file_hints,
    record_decision,
    recall_decisions,
    get_compact_context,
    format_efficiency_injection,
    get_token_stats,
)
from .observe import (
    get_pending_observations,
    promote_observation_to_wisdom,
    auto_promote_high_confidence,
    format_reflection_summary,
)

# Graph is optional (requires kuzu)
try:
    from .graph import (
        KUZU_AVAILABLE,
        get_graph_stats,
        sync_wisdom_to_graph,
        search_concepts,
        activate_from_prompt,
        format_activation_result,
        get_neighbors,
        link_concepts,
        RelationType,
    )
except ImportError:
    KUZU_AVAILABLE = False

from .curiosity import (
    detect_all_gaps,
    run_curiosity_cycle,
    get_pending_questions,
    get_curiosity_stats,
    answer_question,
    dismiss_question,
    incorporate_answer_as_wisdom,
    format_questions_for_prompt,
    GapType,
)

from .narrative import (
    get_episode,
    recall_breakthroughs,
    recall_struggles,
    recall_by_type,
    recall_by_character,
    get_recurring_characters,
    get_emotional_journey,
    format_episode_story,
    get_narrative_stats,
    EpisodeType,
    EmotionalTone,
)

from .neural import (
    create_trigger,
    find_triggers,
    activate,
    activate_with_bridges,
    create_bridge,
    get_trigger_stats,
    sync_wisdom_to_triggers,
    format_neural_context,
    reinforce_trigger,
)


def cmd_summary(args):
    """Show soul summary."""
    init_soul()
    print(summarize_soul())


def cmd_context(args):
    """Show full context dump."""
    import pprint
    init_soul()
    pprint.pprint(get_soul_context())


def cmd_wisdom(args):
    """List wisdom entries."""
    init_soul()
    for w in recall_wisdom(limit=20):
        eff = w.get('effective_confidence', w['confidence'])
        print(f"[{w['type']}] {w['title']} ({eff:.0%})")
        print(f"  {w['content'][:80]}...")
        print()


def cmd_pending(args):
    """Show pending wisdom applications."""
    init_soul()
    pending = get_pending_applications()
    if not pending:
        print("No pending wisdom applications")
    else:
        for p in pending:
            print(f"[{p['id']}] {p['title']}")
            print(f"  Applied: {p['applied_at']}")
            if p['context']:
                print(f"  Context: {p['context'][:60]}...")


def cmd_session(args):
    """Show wisdom applied in current session."""
    init_soul()
    session = get_session_wisdom()
    if not session:
        print("No wisdom applied this session")
    else:
        print(f"Wisdom applied this session: {len(session)}")
        for w in session:
            print(f"  - {w['title']}")
            if w.get('context'):
                print(f"    Context: {w['context'][:50]}...")
            print(f"    Applied: {w['applied_at']}")


def cmd_save(args):
    """Save context for later restoration."""
    init_soul()

    content = args.content
    if not content:
        print("Usage: soul save 'context to remember' [--type insight] [--priority 5]")
        return

    ctx_id = save_context(
        content=content,
        context_type=args.type,
        priority=args.priority
    )
    print(f"Saved context [{args.type}] (id={ctx_id}, priority={args.priority})")


def cmd_restore(args):
    """Show saved context for restoration."""
    init_soul()

    if args.subcommand == 'recent':
        contexts = get_recent_context(hours=args.hours, limit=args.limit)
        if not contexts:
            print(f"No context saved in the last {args.hours} hours")
        else:
            print(format_context_restoration(contexts))

    elif args.subcommand == 'session':
        contexts = get_saved_context(limit=args.limit)
        if not contexts:
            print("No context saved for current session")
        else:
            print(f"Saved context ({len(contexts)} items):\n")
            for ctx in contexts:
                icon = {'insight': '💡', 'decision': '⚖️', 'blocker': '🚧', 'progress': '📊', 'key_file': '📁', 'todo': '☐'}.get(ctx['type'], '•')
                print(f"  {icon} [{ctx['type']}] P{ctx['priority']}: {ctx['content'][:80]}...")

    else:
        # Default: show recent
        contexts = get_recent_context(hours=24, limit=20)
        print(format_context_restoration(contexts))


def cmd_grow(args):
    """Grow the soul - add wisdom, beliefs, identity, vocabulary."""
    init_soul()

    if args.type == 'wisdom':
        if not args.title or not args.content:
            print("Usage: soul grow wisdom 'Title' 'Content'")
            return
        gain_wisdom(WisdomType.PATTERN, args.title, args.content)
        print(f"✓ Wisdom: {args.title}")

    elif args.type == 'insight':
        if not args.title or not args.content:
            print("Usage: soul grow insight 'Title' 'Content'")
            return
        gain_wisdom(WisdomType.INSIGHT, args.title, args.content)
        print(f"✓ Insight: {args.title}")

    elif args.type == 'fail':
        if not args.title or not args.content:
            print("Usage: soul grow fail 'What failed' 'Why and what was learned'")
            return
        gain_wisdom(WisdomType.FAILURE, args.title, args.content)
        print(f"✓ Failure recorded: {args.title}")

    elif args.type == 'belief':
        if not args.title:
            print("Usage: soul grow belief 'Belief statement'")
            return
        hold_belief(args.title, args.content or "")
        print(f"✓ Belief: {args.title}")

    elif args.type == 'identity':
        if not args.title or not args.content:
            print("Usage: soul grow identity 'key' 'value'")
            return
        observe_identity(IdentityAspect.WORKFLOW, args.title, args.content)
        print(f"✓ Identity: {args.title}")

    elif args.type == 'vocab':
        if not args.title or not args.content:
            print("Usage: soul grow vocab 'term' 'meaning'")
            return
        learn_term(args.title, args.content)
        print(f"✓ Vocabulary: {args.title}")

    else:
        print("Usage: soul grow <type> 'title' 'content'")
        print("Types: wisdom, insight, fail, belief, identity, vocab")


def cmd_stats(args):
    """Show wisdom analytics and usage patterns."""
    init_soul()

    if args.subcommand == 'health':
        health = get_wisdom_health()
        timeline = get_wisdom_timeline(days=args.days)
        print(format_wisdom_stats(health, timeline))

    elif args.subcommand == 'timeline':
        timeline = get_wisdom_timeline(days=args.days, bucket=args.bucket)
        if not timeline:
            print("No wisdom applications in this period")
        else:
            print(f"Wisdom applications (last {args.days} days, by {args.bucket}):\n")
            for bucket in timeline:
                apps = bucket["applications"]
                success_rate = bucket["successes"] / apps * 100 if apps else 0
                bar = "█" * min(30, apps)
                print(f"  {bucket['period']}: {bar} {apps} ({success_rate:.0f}%)")

    elif args.subcommand == 'top':
        health = get_wisdom_health()
        if health.get("top_performers"):
            print("Top performing wisdom:\n")
            for w in health["top_performers"]:
                print(f"  ✓ {w['title']}")
                print(f"    Success rate: {w['success_rate']:.0%}, Applications: {w['total_applications']}")
                print()
        else:
            print("No wisdom with enough applications to rank")

    elif args.subcommand == 'issues':
        health = get_wisdom_health()
        has_issues = False

        if health.get("decaying"):
            has_issues = True
            print("Decaying wisdom (confidence dropping):\n")
            for w in health["decaying"][:5]:
                print(f"  ↓ {w['title']}")
                print(f"    Effective confidence: {w['effective_confidence']:.0%}, Inactive: {w['inactive_days']}d")
            print()

        if health.get("failing"):
            has_issues = True
            print("Failing wisdom (>50% failure rate):\n")
            for w in health["failing"]:
                print(f"  ✗ {w['title']}")
                print(f"    Success rate: {w['success_rate']:.0%}, Applications: {w['total_applications']}")
            print()

        if health.get("stale"):
            has_issues = True
            print("Stale wisdom (never applied):\n")
            for w in health["stale"][:5]:
                print(f"  ? {w['title']}")
                print(f"    Age: {w['age_days']}d, Type: {w['type']}")
            print()

        if not has_issues:
            print("No issues found - all wisdom is healthy!")

    elif args.subcommand == 'decay':
        decay_data = get_decay_visualization(limit=args.limit)
        print(format_decay_chart(decay_data))


def cmd_trends(args):
    """Show cross-session trends and growth trajectory."""
    init_soul()

    if args.subcommand == 'growth':
        trajectory = get_growth_trajectory(days=args.days)
        comparison = get_session_comparison(session_count=args.sessions)
        patterns = get_learning_patterns()
        print(format_trends_report(comparison, trajectory, patterns))

    elif args.subcommand == 'sessions':
        comparison = get_session_comparison(session_count=args.sessions)
        print(f"Session Analysis ({comparison['sessions_analyzed']} sessions)\n")
        print(f"Total wisdom gained: {comparison['total_wisdom_gained']}")
        print(f"Avg per session: {comparison['avg_wisdom_per_session']}\n")
        for s in comparison['sessions']:
            gained = f"+{s['wisdom_gained']}" if s['wisdom_gained'] > 0 else "0"
            project = s['project'] or 'unknown'
            print(f"  {s['date']}: {project[:25]} ({gained} wisdom, {s['wisdom_applied']} applied)")
            if s['summary']:
                print(f"    {s['summary'][:60]}...")

    elif args.subcommand == 'patterns':
        patterns = get_learning_patterns()
        print("Learning Patterns\n")
        print(f"Recent wisdom analyzed: {patterns['recent_wisdom_count']}")
        print(f"Dominant type: {patterns['dominant_type']}")
        print(f"Growing domains: {', '.join(patterns['growing_domains'])}")
        if patterns['temporal_patterns']['peak_hour'] is not None:
            print(f"Peak learning: {patterns['temporal_patterns']['peak_day']}s at {patterns['temporal_patterns']['peak_hour']}:00")
        print(f"\nType distribution: {patterns['type_distribution']}")
        print(f"Domain distribution: {patterns['domain_distribution']}")

    elif args.subcommand == 'velocity':
        trajectory = get_growth_trajectory(days=args.days)
        print(f"Learning Velocity ({trajectory['period_days']} days)\n")
        print(f"Total wisdom gained: {trajectory['total_wisdom_gained']}")
        print(f"Domains explored: {trajectory['total_domains']}")
        print(f"Weekly velocity: {trajectory['avg_weekly_velocity']} wisdom/week")
        print(f"Recent velocity: {trajectory['recent_velocity']} wisdom/week")
        print(f"Trend: {trajectory['velocity_trend']}")
        if trajectory.get('trajectory'):
            print("\nWeekly progress:")
            for t in trajectory['trajectory'][-12:]:
                bar = "█" * min(30, t['gained'])
                new_d = f" +{len(t['new_domains'])} domains" if t['new_domains'] else ""
                print(f"  {t['week']}: {bar} {t['gained']}{new_d}")


# Global ultrathink context (persists during session)
_ultrathink_ctx = None


def cmd_ultrathink(args):
    """Soul-integrated ultrathink mode."""
    global _ultrathink_ctx
    init_soul()

    if args.subcommand == 'enter':
        problem = args.problem or ""
        if not problem:
            print("Usage: soul ultrathink enter 'Problem statement'")
            return

        _ultrathink_ctx = enter_ultrathink(problem, domain=args.domain)
        print(format_ultrathink_context(_ultrathink_ctx))

    elif args.subcommand == 'context':
        if _ultrathink_ctx:
            print(format_ultrathink_context(_ultrathink_ctx))
        else:
            print("No active ultrathink session. Use 'soul ultrathink enter' first.")

    elif args.subcommand == 'discover':
        if not _ultrathink_ctx:
            print("No active ultrathink session. Use 'soul ultrathink enter' first.")
            return

        discovery = args.discovery or ""
        if not discovery:
            print("Usage: soul ultrathink discover 'Key insight discovered'")
            return

        record_discovery(_ultrathink_ctx, discovery)
        print(f"Recorded discovery: {discovery[:60]}...")

    elif args.subcommand == 'exit':
        if not _ultrathink_ctx:
            print("No active ultrathink session.")
            return

        summary = args.summary or "Session completed"
        reflection = exit_ultrathink(_ultrathink_ctx, summary)

        print("\n" + "=" * 60)
        print("ULTRATHINK SESSION REFLECTION")
        print("=" * 60)
        print(f"\nDuration: {reflection.duration_minutes:.1f} minutes")
        print(f"Wisdom applied: {reflection.wisdom_applied_count}")
        print(f"Discoveries: {len(reflection.discoveries)}")
        print(f"\nGrowth: {reflection.growth_summary}")

        if reflection.discoveries:
            print("\nDiscoveries made:")
            for d in reflection.discoveries:
                print(f"  - {d['discovery'][:60]}...")

            commit = input("\nCommit discoveries as wisdom? [y/N] ").strip().lower()
            if commit == 'y':
                ids = commit_session_learnings(reflection)
                print(f"Committed {len(ids)} wisdom items")

        _ultrathink_ctx = None


def cmd_efficiency(args):
    """Token efficiency features - problem patterns, file hints, decisions."""
    init_soul()

    if args.subcommand == 'stats':
        stats = get_token_stats()
        print("Token Efficiency Statistics\n")
        print(f"Problem patterns: {stats['problem_patterns']}")
        print(f"Pattern matches: {stats['pattern_matches']}")
        print(f"File hints: {stats['file_hints']}")
        print(f"Decisions recorded: {stats['decisions']}")
        print(f"\nEstimated tokens saved: ~{stats['estimated_tokens_saved']:,}")

    elif args.subcommand == 'learn':
        if not args.pattern_type or not args.solution:
            print("Usage: soul efficiency learn 'pattern description' --type bug --solution 'how to solve'")
            return
        prompt = args.pattern_type  # Using positional as description
        learn_problem_pattern(
            prompt=prompt,
            problem_type=args.type,
            solution_pattern=args.solution,
            file_hints=args.files.split(',') if args.files else None
        )
        print(f"Learned pattern: [{args.type}] {prompt[:50]}...")

    elif args.subcommand == 'hint':
        if not args.file_path or not args.purpose:
            print("Usage: soul efficiency hint '/path/to/file.py' 'Purpose description'")
            return
        add_file_hint(
            file_path=args.file_path,
            purpose=args.purpose,
            key_functions=args.functions.split(',') if args.functions else None,
            related_to=args.related.split(',') if args.related else None
        )
        print(f"Added hint: {args.file_path}")

    elif args.subcommand == 'decide':
        if not args.topic or not args.decision:
            print("Usage: soul efficiency decide 'Topic' 'Decision made'")
            return
        record_decision(
            topic=args.topic,
            decision=args.decision,
            rationale=args.rationale or "",
            context=args.context or ""
        )
        print(f"Recorded decision: {args.topic}")

    elif args.subcommand == 'decisions':
        decisions = recall_decisions(topic=args.topic, limit=args.limit)
        if not decisions:
            print("No decisions recorded")
        else:
            for d in decisions:
                print(f"[{d['made_at'][:10]}] {d['topic']}")
                print(f"  Decision: {d['decision'][:60]}...")
                if d['rationale']:
                    print(f"  Rationale: {d['rationale'][:50]}...")
                print()

    elif args.subcommand == 'compact':
        ctx = get_compact_context(project=args.project)
        print(ctx)

    elif args.subcommand == 'check':
        if not args.prompt:
            print("Usage: soul efficiency check 'problem description'")
            return
        prompt = ' '.join(args.prompt)
        injection = format_efficiency_injection(prompt)
        if injection:
            print(injection)
        else:
            print("No efficiency hints for this prompt")


def cmd_observe(args):
    """Manage passive observations from sessions."""
    init_soul()

    if args.subcommand == 'pending':
        observations = get_pending_observations(limit=args.limit)
        if not observations:
            print("No pending observations")
        else:
            print(f"Pending observations ({len(observations)}):\n")
            type_icons = {
                'correction': '🔄',
                'preference': '👤',
                'pattern': '🔁',
                'struggle': '💪',
                'breakthrough': '💡',
                'file_pattern': '📁',
                'decision': '⚖️',
            }
            for obs in observations:
                icon = type_icons.get(obs['type'], '•')
                conf = obs['confidence']
                print(f"  {icon} [{obs['id']}] ({conf:.0%}) {obs['content'][:60]}...")

    elif args.subcommand == 'promote':
        if args.id:
            wisdom_id = promote_observation_to_wisdom(args.id)
            if wisdom_id:
                print(f"Promoted observation {args.id} to wisdom {wisdom_id}")
            else:
                print(f"Observation {args.id} not found")
        elif args.all:
            promoted = auto_promote_high_confidence(threshold=args.threshold)
            print(f"Promoted {len(promoted)} high-confidence observations")
        else:
            print("Usage: soul observe promote <id> or soul observe promote --all")

    elif args.subcommand == 'stats':
        observations = get_pending_observations(limit=100)
        from collections import Counter
        by_type = Counter(obs['type'] for obs in observations)
        print(f"Pending observations: {len(observations)}\n")
        print("By type:")
        for t, count in by_type.most_common():
            print(f"  {t}: {count}")


def cmd_graph(args):
    """Concept graph exploration and management."""
    init_soul()

    if not KUZU_AVAILABLE:
        print("Graph module not available. Install with: pip install cc-soul[graph]")
        return

    if args.subcommand == 'stats':
        stats = get_graph_stats()
        print("Concept Graph Statistics\n")
        print(f"Total concepts: {stats['nodes']}")
        print(f"Total edges: {stats['edges']}")
        if stats['by_type']:
            print("\nBy type:")
            for t, count in stats['by_type'].items():
                print(f"  {t}: {count}")
        if stats['by_relation']:
            print("\nBy relation:")
            for r, count in stats['by_relation'].items():
                print(f"  {r}: {count}")

    elif args.subcommand == 'sync':
        print("Syncing soul data to concept graph...")
        stats = sync_wisdom_to_graph()
        print(f"Synced {stats['nodes']} concepts with {stats['edges']} relationships")

    elif args.subcommand == 'search':
        if not args.query:
            print("Usage: soul graph search 'query'")
            return
        query = ' '.join(args.query)
        concepts = search_concepts(query, limit=args.limit)
        if not concepts:
            print(f"No concepts matching '{query}'")
        else:
            print(f"Concepts matching '{query}':\n")
            for c in concepts:
                print(f"  [{c.type.value}] {c.title}")
                print(f"    {c.content[:60]}...")

    elif args.subcommand == 'activate':
        if not args.prompt:
            print("Usage: soul graph activate 'prompt text'")
            return
        prompt = ' '.join(args.prompt)
        result = activate_from_prompt(prompt, limit=args.limit)
        print(format_activation_result(result))

    elif args.subcommand == 'neighbors':
        if not args.concept_id:
            print("Usage: soul graph neighbors <concept_id>")
            return
        neighbors = get_neighbors(args.concept_id, limit=args.limit)
        if not neighbors:
            print(f"No neighbors for concept '{args.concept_id}'")
        else:
            print(f"Neighbors of '{args.concept_id}':\n")
            for concept, edge in neighbors:
                print(f"  --[{edge.relation.value} {edge.weight:.2f}]--> {concept.title}")

    elif args.subcommand == 'link':
        if not args.source or not args.target:
            print("Usage: soul graph link <source_id> <target_id> --relation <type>")
            return
        try:
            relation = RelationType(args.relation)
        except ValueError:
            print(f"Invalid relation. Options: {', '.join(r.value for r in RelationType)}")
            return
        success = link_concepts(args.source, args.target, relation, evidence=args.evidence or "")
        if success:
            print(f"Linked {args.source} --[{relation.value}]--> {args.target}")
        else:
            print("Failed to create link. Check that both concepts exist.")


def cmd_curious(args):
    """Curiosity engine - detect gaps and ask questions."""
    init_soul()

    if args.subcommand == 'gaps':
        gaps = detect_all_gaps()
        if not gaps:
            print("No knowledge gaps detected")
        else:
            type_icons = {
                'recurring_problem': '🔄',
                'repeated_correction': '✏️',
                'unknown_file': '📁',
                'missing_rationale': '❓',
                'new_domain': '🌍',
                'stale_wisdom': '📦',
                'failed_pattern': '❌',
            }
            print(f"Knowledge Gaps ({len(gaps)}):\n")
            for gap in gaps[:args.limit]:
                icon = type_icons.get(gap.type.value, '•')
                print(f"  {icon} [{gap.priority:.0%}] {gap.description[:60]}...")
                if gap.evidence:
                    print(f"      Evidence: {gap.evidence[0][:50]}...")

    elif args.subcommand == 'questions':
        questions = get_pending_questions(limit=args.limit)
        if not questions:
            print("No pending questions")
        else:
            print(f"Pending Questions ({len(questions)}):\n")
            for q in questions:
                print(f"  [{q.id}] ({q.priority:.0%}) {q.question}")
                if q.context:
                    print(f"      Context: {q.context[:50]}...")

    elif args.subcommand == 'ask':
        questions = run_curiosity_cycle(max_questions=args.limit)
        if not questions:
            print("No questions to ask right now")
        else:
            print(format_questions_for_prompt(questions, max_questions=args.limit))

    elif args.subcommand == 'answer':
        if not args.id or not args.answer:
            print("Usage: soul curious answer <question_id> 'your answer'")
            return
        answer_text = ' '.join(args.answer)
        success = answer_question(args.id, answer_text, incorporate=args.incorporate)
        if success:
            print(f"Recorded answer for question {args.id}")
            if args.incorporate:
                wisdom_id = incorporate_answer_as_wisdom(args.id)
                if wisdom_id:
                    print(f"Created wisdom entry {wisdom_id}")
        else:
            print(f"Question {args.id} not found")

    elif args.subcommand == 'dismiss':
        if not args.id:
            print("Usage: soul curious dismiss <question_id>")
            return
        success = dismiss_question(args.id)
        if success:
            print(f"Dismissed question {args.id}")
        else:
            print(f"Question {args.id} not found")

    elif args.subcommand == 'stats':
        stats = get_curiosity_stats()
        print("Curiosity Engine Statistics\n")
        print(f"Open gaps: {stats['open_gaps']}")
        if stats['gaps_by_type']:
            print("\nGaps by type:")
            for t, count in stats['gaps_by_type'].items():
                print(f"  {t}: {count}")
        print(f"\nQuestions:")
        print(f"  Pending: {stats['questions']['pending']}")
        print(f"  Answered: {stats['questions']['answered']}")
        print(f"  Incorporated: {stats['questions']['incorporated']}")
        print(f"  Dismissed: {stats['questions']['dismissed']}")
        print(f"\nIncorporation rate: {stats['incorporation_rate']:.0%}")


def cmd_story(args):
    """Narrative memory - stories and episodes."""
    init_soul()

    if args.subcommand == 'stats':
        stats = get_narrative_stats()
        print("Narrative Memory Statistics\n")
        print(f"Total episodes: {stats['total_episodes']}")
        print(f"Story threads: {stats['total_threads']} ({stats['ongoing_threads']} ongoing)")
        print(f"Total time: {stats['total_hours']} hours")
        if stats['by_type']:
            print("\nBy type:")
            for t, count in stats['by_type'].items():
                print(f"  {t}: {count}")

    elif args.subcommand == 'breakthroughs':
        episodes = recall_breakthroughs(limit=args.limit)
        if not episodes:
            print("No breakthrough moments recorded yet")
        else:
            print(f"Breakthrough Moments ({len(episodes)}):\n")
            for ep in episodes:
                print(format_episode_story(ep))
                print("\n---\n")

    elif args.subcommand == 'struggles':
        episodes = recall_struggles(limit=args.limit)
        if not episodes:
            print("No struggle moments recorded yet")
        else:
            print(f"Learning from Struggles ({len(episodes)}):\n")
            for ep in episodes:
                print(format_episode_story(ep))
                print("\n---\n")

    elif args.subcommand == 'journey':
        journey = get_emotional_journey(days=args.days)
        print(f"Emotional Journey (last {args.days} days)\n")
        if journey['dominant']:
            print(f"Dominant emotion: {journey['dominant']}")
        print(f"Breakthroughs: {journey['breakthroughs']}")
        print(f"Struggles: {journey['struggles']}")
        if journey['distribution']:
            print("\nDistribution:")
            for emotion, pct in sorted(journey['distribution'].items(), key=lambda x: -x[1]):
                bar = '█' * int(pct * 20)
                print(f"  {emotion:15} {bar} {pct:.0%}")

    elif args.subcommand == 'characters':
        chars = get_recurring_characters(limit=args.limit)
        print("Recurring Characters\n")
        if chars['files']:
            print("Files:")
            for f, count in chars['files'][:10]:
                print(f"  {f}: {count} episodes")
        if chars['concepts']:
            print("\nConcepts:")
            for c, count in chars['concepts'][:10]:
                print(f"  {c}: {count} episodes")
        if chars['tools']:
            print("\nTools:")
            for t, count in chars['tools'][:10]:
                print(f"  {t}: {count} episodes")

    elif args.subcommand == 'episode':
        if not args.id:
            print("Usage: soul story episode <id>")
            return
        ep = get_episode(args.id)
        if ep:
            print(format_episode_story(ep))
        else:
            print(f"Episode {args.id} not found")

    elif args.subcommand == 'recall':
        if args.type:
            try:
                ep_type = EpisodeType(args.type)
                episodes = recall_by_type(ep_type, limit=args.limit)
            except ValueError:
                print(f"Unknown type. Options: {', '.join(t.value for t in EpisodeType)}")
                return
        elif args.character:
            episodes = recall_by_character(args.character, limit=args.limit)
        else:
            print("Usage: soul story recall --type <type> or --character <name>")
            return

        if not episodes:
            print("No matching episodes")
        else:
            for ep in episodes:
                print(f"[{ep.id}] {ep.title}")
                if ep.summary:
                    print(f"    {ep.summary[:60]}...")


def cmd_neural(args):
    """Neural triggers - activation keys for Claude's latent knowledge."""
    init_soul()

    if args.subcommand == 'stats':
        stats = get_trigger_stats()
        print("Neural Trigger Statistics\n")
        print(f"Total triggers: {stats.get('total_triggers', 0)}")
        print(f"Total bridges: {stats.get('total_bridges', 0)}")
        print(f"Total uses: {stats.get('total_uses', 0)}")
        if stats.get('avg_strength'):
            print(f"Avg strength: {stats['avg_strength']:.2f}")
        if stats.get('domains'):
            print(f"\nDomains: {', '.join(stats['domains'][:10])}")

    elif args.subcommand == 'sync':
        print("Converting wisdom to neural triggers...")
        result = sync_wisdom_to_triggers()
        print(f"Processed {result['wisdom_count']} wisdom entries")
        print(f"Created {result['triggers_created']} triggers")

    elif args.subcommand == 'activate':
        if not args.prompt:
            print("Usage: soul neural activate 'your prompt'")
            return
        prompt = ' '.join(args.prompt)
        activation = activate_with_bridges(prompt)
        if activation:
            print("Neural activation key:\n")
            print(activation)
        else:
            print("No triggers matched this prompt")

    elif args.subcommand == 'context':
        if not args.prompt:
            print("Usage: soul neural context 'your prompt'")
            return
        prompt = ' '.join(args.prompt)
        ctx = format_neural_context(prompt)
        if ctx:
            print("Inject this into context:\n")
            print(ctx)
        else:
            print("No neural context generated")

    elif args.subcommand == 'extract':
        if not args.text or not args.domain:
            print("Usage: soul neural extract --domain <domain> 'text'")
            return
        text = ' '.join(args.text)
        trigger = create_trigger(text, args.domain)
        print(f"Created trigger: {trigger.id}")
        print(f"Domain: {trigger.domain}")
        print(f"Anchor tokens: {' '.join(trigger.anchor_tokens)}")

    elif args.subcommand == 'bridge':
        if not args.source or not args.target:
            print("Usage: soul neural bridge <source_domain> <target_domain> --via 'connecting text'")
            return
        via = ' '.join(args.via) if args.via else f"{args.source} {args.target}"
        bridge = create_bridge(args.source, args.target, via, args.evidence or "")
        print(f"Bridge created: {args.source} <-> {args.target}")
        print(f"Via: {' '.join(bridge.bridge_tokens)}")

    elif args.subcommand == 'find':
        if not args.prompt:
            print("Usage: soul neural find 'prompt'")
            return
        prompt = ' '.join(args.prompt)
        triggers = find_triggers(prompt, top_k=args.limit)
        if not triggers:
            print("No matching triggers")
        else:
            print(f"Found {len(triggers)} triggers:\n")
            for trigger, score in triggers:
                print(f"  [{score:.0%}] {trigger.domain}")
                print(f"       {' '.join(trigger.anchor_tokens)}")


def cmd_reindex(args):
    """Reindex wisdom vectors."""
    init_soul()
    reindex_all_wisdom()


def cmd_install_skills(args):
    """Install bundled skills to ~/.claude/skills."""
    import importlib.resources as pkg_resources

    skills_dest = Path.home() / ".claude" / "skills"
    skills_dest.mkdir(parents=True, exist_ok=True)

    try:
        skills_src = Path(pkg_resources.files("cc_soul") / "skills")
    except (TypeError, AttributeError):
        import pkg_resources as old_pkg
        skills_src = Path(old_pkg.resource_filename("cc_soul", "skills"))

    if not skills_src.exists():
        print(f"Error: Skills directory not found in package at {skills_src}")
        return 1

    installed = []
    skipped = []

    for skill_dir in skills_src.iterdir():
        if not skill_dir.is_dir():
            continue

        dest_dir = skills_dest / skill_dir.name

        if dest_dir.exists() and not args.force:
            skipped.append(skill_dir.name)
            continue

        if dest_dir.exists():
            shutil.rmtree(dest_dir)

        shutil.copytree(skill_dir, dest_dir)
        installed.append(skill_dir.name)

    if installed:
        print(f"Installed skills: {', '.join(installed)}")
    if skipped:
        print(f"Skipped (already exist, use --force to overwrite): {', '.join(skipped)}")
    if not installed and not skipped:
        print("No skills found in package")

    return 0


def cmd_hook(args):
    """Run a hook."""
    init_soul()

    if args.hook == 'start':
        print(session_start())
    elif args.hook == 'end':
        print(session_end())
    elif args.hook == 'prompt':
        if args.input:
            text = " ".join(args.input)
        else:
            text = sys.stdin.read()
        output = user_prompt(text)
        if output:
            print(output)


def cmd_evolve(args):
    """Manage soul evolution insights."""
    init_soul()
    seed_evolution_insights()

    if args.subcommand == 'list':
        insights = get_evolution_insights(category=args.category, limit=args.limit)
        for i in insights:
            priority_icon = {'critical': '🔴', 'high': '🟠', 'medium': '🟡', 'low': '🟢'}.get(i['priority'], '⚪')
            print(f"{priority_icon} [{i['category']}] {i['insight'][:60]}...")
            if i['suggested_change']:
                print(f"   → {i['suggested_change'][:60]}...")
            print()

    elif args.subcommand == 'summary':
        summary = get_evolution_summary()
        print(f"Total insights: {summary['total']}")
        print(f"Open: {summary['open']}, Implemented: {summary['implemented']}")
        print(f"High priority open: {summary['high_priority_open']}")
        print("\nBy category:")
        for cat, count in summary['by_category'].items():
            print(f"  {cat}: {count}")


def cmd_introspect(args):
    """Run self-introspection."""
    init_soul()

    if args.subcommand == 'report':
        report = generate_introspection_report()
        if args.json:
            import json
            print(json.dumps(report, indent=2, default=str))
        else:
            print(format_introspection_report(report))

    elif args.subcommand == 'pain':
        pain = analyze_pain_points()
        print(f"Open pain points: {pain['total_open']}")
        if pain['by_severity']:
            print(f"By severity: {pain['by_severity']}")
        if pain['by_category']:
            print(f"By category: {pain['by_category']}")
        print("\nRecent:")
        for p in pain.get('recent', [])[:5]:
            icon = {'critical': '🔴', 'high': '🟠', 'medium': '🟡', 'low': '🟢'}.get(p['severity'], '⚪')
            print(f"  {icon} [{p['category']}] {p['description'][:60]}...")

    elif args.subcommand == 'diagnose':
        diag = diagnose()
        print(f"Improvement targets: {diag['summary']['total_targets']}")
        print(f"  Critical: {diag['summary']['critical']}")
        print(f"  High: {diag['summary']['high']}")
        print("\nTop targets:")
        for t in diag['targets'][:5]:
            print(f"  P{t['priority']}: [{t['type']}] {t['description'][:50]}...")


def cmd_improve(args):
    """Manage improvements."""
    init_soul()

    if args.subcommand == 'suggest':
        suggestions = suggest_improvements(limit=args.limit)
        for i, s in enumerate(suggestions, 1):
            print(f"\n{'='*60}")
            print(f"Suggestion {i}: {s['target']['description'][:60]}...")
            print(f"Type: {s['target']['type']}, Priority: {s['target']['priority']}")
            print(f"\nPrompt for Claude:")
            print(s['prompt'])

    elif args.subcommand == 'proposals':
        proposals = get_proposals(limit=args.limit)
        if not proposals:
            print("No proposals yet")
        else:
            for p in proposals:
                status_icon = {
                    'proposed': '📝',
                    'validating': '🔄',
                    'validated': '✓',
                    'applying': '⚙️',
                    'applied': '✅',
                    'failed': '❌',
                    'rejected': '🚫',
                }.get(p['status'], '?')
                print(f"{status_icon} [{p['id'][:15]}...] {p['title']}")
                print(f"   {p['description'][:60]}...")

    elif args.subcommand == 'stats':
        stats = get_improvement_stats()
        print(f"Total improvements: {stats['total']}")
        print(f"Success rate: {stats['success_rate']:.0%}")
        print(f"Successes: {stats.get('successes', 0)}, Failures: {stats.get('failures', 0)}")
        if stats.get('by_category'):
            print("\nBy category:")
            for cat, count in stats['by_category'].items():
                print(f"  {cat}: {count}")


def main():
    parser = argparse.ArgumentParser(
        description='CC-Soul: Persistent Identity for Claude Code',
        prog='soul'
    )
    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # Summary (default)
    subparsers.add_parser('summary', help='Show soul summary')

    # Context
    subparsers.add_parser('context', help='Show full context dump')

    # Wisdom
    subparsers.add_parser('wisdom', help='List wisdom entries')

    # Pending
    subparsers.add_parser('pending', help='Show pending wisdom applications')

    # Session
    subparsers.add_parser('session', help='Show wisdom applied this session')

    # Save (context persistence)
    save_parser = subparsers.add_parser('save', help='Save context for later restoration')
    save_parser.add_argument('content', nargs='?', help='Context to save')
    save_parser.add_argument('--type', choices=['insight', 'decision', 'blocker', 'progress', 'key_file', 'todo'],
                            default='insight', help='Type of context')
    save_parser.add_argument('--priority', type=int, default=5, help='Priority 1-10 (higher = more important)')

    # Restore (context restoration)
    restore_parser = subparsers.add_parser('restore', help='Restore saved context')
    restore_subs = restore_parser.add_subparsers(dest='subcommand')

    recent_parser = restore_subs.add_parser('recent', help='Show context from last N hours')
    recent_parser.add_argument('--hours', type=int, default=24, help='Hours to look back')
    recent_parser.add_argument('--limit', type=int, default=20, help='Max items to show')

    session_ctx_parser = restore_subs.add_parser('session', help='Show context from current session')
    session_ctx_parser.add_argument('--limit', type=int, default=20, help='Max items to show')

    # Grow
    grow_parser = subparsers.add_parser('grow', help='Grow the soul (add wisdom, beliefs, etc)')
    grow_parser.add_argument('type', choices=['wisdom', 'insight', 'fail', 'belief', 'identity', 'vocab'],
                            help='Type of entry to add')
    grow_parser.add_argument('title', nargs='?', help='Title or key')
    grow_parser.add_argument('content', nargs='?', help='Content or value')

    # Reindex
    subparsers.add_parser('reindex', help='Reindex wisdom vectors')

    # Install skills
    install_parser = subparsers.add_parser('install-skills', help='Install bundled skills to ~/.claude/skills')
    install_parser.add_argument('--force', action='store_true', help='Overwrite existing skills')

    # Hook
    hook_parser = subparsers.add_parser('hook', help='Run a Claude Code hook')
    hook_parser.add_argument('hook', choices=['start', 'end', 'prompt'])
    hook_parser.add_argument('input', nargs='*', help='Input for prompt hook')

    # Evolve
    evolve_parser = subparsers.add_parser('evolve', help='Manage evolution insights')
    evolve_subs = evolve_parser.add_subparsers(dest='subcommand')

    list_parser = evolve_subs.add_parser('list', help='List evolution insights')
    list_parser.add_argument('--category', help='Filter by category')
    list_parser.add_argument('--limit', type=int, default=20)

    evolve_subs.add_parser('summary', help='Show evolution summary')

    # Introspect
    intro_parser = subparsers.add_parser('introspect', help='Self-introspection')
    intro_subs = intro_parser.add_subparsers(dest='subcommand')

    report_parser = intro_subs.add_parser('report', help='Generate introspection report')
    report_parser.add_argument('--json', action='store_true', help='Output as JSON')

    intro_subs.add_parser('pain', help='Show pain points')
    intro_subs.add_parser('diagnose', help='Diagnose improvement targets')

    # Improve
    imp_parser = subparsers.add_parser('improve', help='Self-improvement')
    imp_subs = imp_parser.add_subparsers(dest='subcommand')

    suggest_parser = imp_subs.add_parser('suggest', help='Suggest improvements')
    suggest_parser.add_argument('--limit', type=int, default=3)

    proposals_parser = imp_subs.add_parser('proposals', help='List proposals')
    proposals_parser.add_argument('--limit', type=int, default=20)

    imp_subs.add_parser('stats', help='Show improvement statistics')

    # Stats (wisdom analytics)
    stats_parser = subparsers.add_parser('stats', help='Wisdom analytics and usage patterns')
    stats_subs = stats_parser.add_subparsers(dest='subcommand')

    health_parser = stats_subs.add_parser('health', help='Overall wisdom health report')
    health_parser.add_argument('--days', type=int, default=30, help='Days to analyze')

    timeline_parser = stats_subs.add_parser('timeline', help='Application timeline')
    timeline_parser.add_argument('--days', type=int, default=30, help='Days to analyze')
    timeline_parser.add_argument('--bucket', choices=['day', 'week', 'month'], default='day')

    stats_subs.add_parser('top', help='Top performing wisdom')
    stats_subs.add_parser('issues', help='Wisdom issues (decaying, failing, stale)')

    decay_parser = stats_subs.add_parser('decay', help='Visualize wisdom confidence decay over time')
    decay_parser.add_argument('--limit', type=int, default=20, help='Number of wisdom items to show')

    # Trends (cross-session analysis)
    trends_parser = subparsers.add_parser('trends', help='Cross-session trends and growth trajectory')
    trends_subs = trends_parser.add_subparsers(dest='subcommand')

    growth_parser = trends_subs.add_parser('growth', help='Full growth report')
    growth_parser.add_argument('--days', type=int, default=90, help='Days to analyze')
    growth_parser.add_argument('--sessions', type=int, default=10, help='Recent sessions to compare')

    sessions_parser = trends_subs.add_parser('sessions', help='Session-by-session analysis')
    sessions_parser.add_argument('--sessions', type=int, default=10, help='Number of sessions')

    trends_subs.add_parser('patterns', help='Learning patterns analysis')

    velocity_parser = trends_subs.add_parser('velocity', help='Learning velocity over time')
    velocity_parser.add_argument('--days', type=int, default=90, help='Days to analyze')

    # Ultrathink (soul-integrated deep reasoning)
    ultra_parser = subparsers.add_parser('ultrathink', help='Soul-integrated deep reasoning mode')
    ultra_subs = ultra_parser.add_subparsers(dest='subcommand')

    enter_parser = ultra_subs.add_parser('enter', help='Enter ultrathink mode with problem statement')
    enter_parser.add_argument('problem', nargs='?', help='Problem statement')
    enter_parser.add_argument('--domain', help='Domain hint (bioinformatics, web, cli, etc)')

    ultra_subs.add_parser('context', help='Show current ultrathink context')

    discover_parser = ultra_subs.add_parser('discover', help='Record a discovery during reasoning')
    discover_parser.add_argument('discovery', nargs='?', help='The discovery/insight')

    exit_parser = ultra_subs.add_parser('exit', help='Exit ultrathink and reflect')
    exit_parser.add_argument('summary', nargs='?', help='Session summary')

    # Efficiency (token-saving features)
    eff_parser = subparsers.add_parser('efficiency', help='Token efficiency features')
    eff_subs = eff_parser.add_subparsers(dest='subcommand')

    eff_subs.add_parser('stats', help='Show token efficiency statistics')

    learn_parser = eff_subs.add_parser('learn', help='Learn a problem pattern')
    learn_parser.add_argument('pattern_type', nargs='?', help='Problem description')
    learn_parser.add_argument('--type', choices=['bug', 'feature', 'performance', 'refactor', 'config'],
                              default='bug', help='Problem type')
    learn_parser.add_argument('--solution', help='Solution pattern')
    learn_parser.add_argument('--files', help='Comma-separated file hints')

    hint_parser = eff_subs.add_parser('hint', help='Add file hint')
    hint_parser.add_argument('file_path', nargs='?', help='File path')
    hint_parser.add_argument('purpose', nargs='?', help='Purpose of file')
    hint_parser.add_argument('--functions', help='Comma-separated key functions')
    hint_parser.add_argument('--related', help='Comma-separated related keywords')

    decide_parser = eff_subs.add_parser('decide', help='Record a decision')
    decide_parser.add_argument('topic', nargs='?', help='Decision topic')
    decide_parser.add_argument('decision', nargs='?', help='The decision made')
    decide_parser.add_argument('--rationale', help='Why this decision')
    decide_parser.add_argument('--context', help='Additional context')

    decisions_parser = eff_subs.add_parser('decisions', help='List past decisions')
    decisions_parser.add_argument('--topic', help='Filter by topic')
    decisions_parser.add_argument('--limit', type=int, default=10, help='Max items')

    compact_parser = eff_subs.add_parser('compact', help='Show compact context')
    compact_parser.add_argument('--project', help='Filter by project')

    check_parser = eff_subs.add_parser('check', help='Check efficiency hints for a prompt')
    check_parser.add_argument('prompt', nargs='*', help='Prompt to check')

    # Observe (passive learning)
    obs_parser = subparsers.add_parser('observe', help='Manage passive observations')
    obs_subs = obs_parser.add_subparsers(dest='subcommand')

    pending_parser = obs_subs.add_parser('pending', help='Show pending observations')
    pending_parser.add_argument('--limit', type=int, default=20, help='Max items to show')

    promote_parser = obs_subs.add_parser('promote', help='Promote observation to wisdom')
    promote_parser.add_argument('id', type=int, nargs='?', help='Observation ID to promote')
    promote_parser.add_argument('--all', action='store_true', help='Promote all high-confidence')
    promote_parser.add_argument('--threshold', type=float, default=0.75, help='Confidence threshold')

    obs_subs.add_parser('stats', help='Show observation statistics')

    # Graph (concept graph exploration)
    graph_parser = subparsers.add_parser('graph', help='Concept graph exploration')
    graph_subs = graph_parser.add_subparsers(dest='subcommand')

    graph_subs.add_parser('stats', help='Show graph statistics')
    graph_subs.add_parser('sync', help='Sync soul data to concept graph')

    search_g_parser = graph_subs.add_parser('search', help='Search concepts')
    search_g_parser.add_argument('query', nargs='*', help='Search query')
    search_g_parser.add_argument('--limit', type=int, default=10, help='Max results')

    activate_parser = graph_subs.add_parser('activate', help='Activate concepts from prompt')
    activate_parser.add_argument('prompt', nargs='*', help='Prompt text')
    activate_parser.add_argument('--limit', type=int, default=10, help='Max results')

    neighbors_parser = graph_subs.add_parser('neighbors', help='Show concept neighbors')
    neighbors_parser.add_argument('concept_id', nargs='?', help='Concept ID')
    neighbors_parser.add_argument('--limit', type=int, default=10, help='Max results')

    link_parser = graph_subs.add_parser('link', help='Link two concepts')
    link_parser.add_argument('source', nargs='?', help='Source concept ID')
    link_parser.add_argument('target', nargs='?', help='Target concept ID')
    link_parser.add_argument('--relation', default='related_to',
                             help='Relation type (related_to, led_to, contradicts, etc)')
    link_parser.add_argument('--evidence', help='Evidence for the link')

    # Curious (curiosity engine)
    curious_parser = subparsers.add_parser('curious', help='Curiosity engine - gaps and questions')
    curious_subs = curious_parser.add_subparsers(dest='subcommand')

    gaps_parser = curious_subs.add_parser('gaps', help='Detect knowledge gaps')
    gaps_parser.add_argument('--limit', type=int, default=10, help='Max gaps to show')

    questions_parser = curious_subs.add_parser('questions', help='Show pending questions')
    questions_parser.add_argument('--limit', type=int, default=10, help='Max questions to show')

    ask_parser = curious_subs.add_parser('ask', help='Run curiosity cycle and show questions to ask')
    ask_parser.add_argument('--limit', type=int, default=3, help='Max questions to ask')

    answer_parser = curious_subs.add_parser('answer', help='Answer a question')
    answer_parser.add_argument('id', type=int, nargs='?', help='Question ID')
    answer_parser.add_argument('answer', nargs='*', help='Your answer')
    answer_parser.add_argument('--incorporate', action='store_true', help='Also create wisdom from answer')

    dismiss_parser = curious_subs.add_parser('dismiss', help='Dismiss a question')
    dismiss_parser.add_argument('id', type=int, nargs='?', help='Question ID')

    curious_subs.add_parser('stats', help='Show curiosity statistics')

    # Story (narrative memory)
    story_parser = subparsers.add_parser('story', help='Narrative memory - episodes and stories')
    story_subs = story_parser.add_subparsers(dest='subcommand')

    story_subs.add_parser('stats', help='Show narrative statistics')

    breakthroughs_parser = story_subs.add_parser('breakthroughs', help='Recall breakthrough moments')
    breakthroughs_parser.add_argument('--limit', type=int, default=5, help='Max episodes')

    struggles_parser = story_subs.add_parser('struggles', help='Recall struggle moments')
    struggles_parser.add_argument('--limit', type=int, default=5, help='Max episodes')

    journey_parser = story_subs.add_parser('journey', help='Show emotional journey')
    journey_parser.add_argument('--days', type=int, default=30, help='Days to analyze')

    chars_parser = story_subs.add_parser('characters', help='Show recurring characters')
    chars_parser.add_argument('--limit', type=int, default=10, help='Max characters')

    episode_parser = story_subs.add_parser('episode', help='View a specific episode')
    episode_parser.add_argument('id', type=int, nargs='?', help='Episode ID')

    recall_parser = story_subs.add_parser('recall', help='Recall episodes by type or character')
    recall_parser.add_argument('--type', help='Episode type (bugfix, feature, etc)')
    recall_parser.add_argument('--character', help='Character name (file, concept)')
    recall_parser.add_argument('--limit', type=int, default=10, help='Max episodes')

    # Neural (activation triggers)
    neural_parser = subparsers.add_parser('neural', help='Neural triggers - activation keys for latent knowledge')
    neural_subs = neural_parser.add_subparsers(dest='subcommand')

    neural_subs.add_parser('stats', help='Show trigger statistics')
    neural_subs.add_parser('sync', help='Convert wisdom to neural triggers')

    neural_activate = neural_subs.add_parser('activate', help='Generate activation for a prompt')
    neural_activate.add_argument('prompt', nargs='*', help='Prompt text')

    neural_context = neural_subs.add_parser('context', help='Generate injectable context')
    neural_context.add_argument('prompt', nargs='*', help='Prompt text')

    neural_extract = neural_subs.add_parser('extract', help='Extract trigger from text')
    neural_extract.add_argument('text', nargs='*', help='Text to extract from')
    neural_extract.add_argument('--domain', required=True, help='Knowledge domain')

    neural_bridge = neural_subs.add_parser('bridge', help='Create bridge between domains')
    neural_bridge.add_argument('source', nargs='?', help='Source domain')
    neural_bridge.add_argument('target', nargs='?', help='Target domain')
    neural_bridge.add_argument('--via', nargs='*', help='Connecting text')
    neural_bridge.add_argument('--evidence', help='Why this connection exists')

    neural_find = neural_subs.add_parser('find', help='Find triggers matching a prompt')
    neural_find.add_argument('prompt', nargs='*', help='Prompt text')
    neural_find.add_argument('--limit', type=int, default=5, help='Max results')

    args = parser.parse_args()

    if args.command is None or args.command == 'summary':
        cmd_summary(args)
    elif args.command == 'context':
        cmd_context(args)
    elif args.command == 'wisdom':
        cmd_wisdom(args)
    elif args.command == 'pending':
        cmd_pending(args)
    elif args.command == 'session':
        cmd_session(args)
    elif args.command == 'save':
        cmd_save(args)
    elif args.command == 'restore':
        if args.subcommand:
            cmd_restore(args)
        else:
            cmd_restore(argparse.Namespace(subcommand='recent', hours=24, limit=20))
    elif args.command == 'grow':
        cmd_grow(args)
    elif args.command == 'reindex':
        cmd_reindex(args)
    elif args.command == 'install-skills':
        cmd_install_skills(args)
    elif args.command == 'hook':
        cmd_hook(args)
    elif args.command == 'evolve':
        if args.subcommand:
            cmd_evolve(args)
        else:
            cmd_evolve(argparse.Namespace(subcommand='summary'))
    elif args.command == 'introspect':
        if args.subcommand:
            cmd_introspect(args)
        else:
            cmd_introspect(argparse.Namespace(subcommand='report', json=False))
    elif args.command == 'improve':
        if args.subcommand:
            cmd_improve(args)
        else:
            cmd_improve(argparse.Namespace(subcommand='suggest', limit=3))
    elif args.command == 'stats':
        if args.subcommand:
            cmd_stats(args)
        else:
            cmd_stats(argparse.Namespace(subcommand='health', days=30))
    elif args.command == 'trends':
        if args.subcommand:
            cmd_trends(args)
        else:
            cmd_trends(argparse.Namespace(subcommand='growth', days=90, sessions=10))
    elif args.command == 'ultrathink':
        if args.subcommand:
            cmd_ultrathink(args)
        else:
            print("Usage: soul ultrathink <enter|context|discover|exit>")
    elif args.command == 'efficiency':
        if args.subcommand:
            cmd_efficiency(args)
        else:
            cmd_efficiency(argparse.Namespace(subcommand='stats'))
    elif args.command == 'observe':
        if args.subcommand:
            cmd_observe(args)
        else:
            cmd_observe(argparse.Namespace(subcommand='pending', limit=20))
    elif args.command == 'graph':
        if args.subcommand:
            cmd_graph(args)
        else:
            cmd_graph(argparse.Namespace(subcommand='stats'))
    elif args.command == 'curious':
        if args.subcommand:
            cmd_curious(args)
        else:
            cmd_curious(argparse.Namespace(subcommand='gaps', limit=10))
    elif args.command == 'story':
        if args.subcommand:
            cmd_story(args)
        else:
            cmd_story(argparse.Namespace(subcommand='stats'))
    elif args.command == 'neural':
        if args.subcommand:
            cmd_neural(args)
        else:
            cmd_neural(argparse.Namespace(subcommand='stats'))


if __name__ == '__main__':
    main()
