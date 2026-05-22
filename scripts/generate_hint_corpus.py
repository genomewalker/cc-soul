#!/usr/bin/env python3
"""
Generate diverse training corpus for chitta-hint fine-tuning.

Supersedes generate_hint_training_data.py with:
- Parameterized templates (variable slots, not static strings)
- 35% hard negatives including first-person-on-non-personal content
- Two-stage labeling: fact detection then canonical rendering
- Strict validation + retry
- Full-string dedup
- Per-row metadata

Usage:
    python3 generate_hint_corpus.py \
        --out /maps/projects/caeg/scratch/kbd606/tmp/hint_corpus_v2.jsonl \
        [--model gemma4:26b] [--target 1500] [--dry-run]
"""

import argparse
import glob
import hashlib
import itertools
import json
import os
import random
import re
import sys
import time
import urllib.request

# ---------------------------------------------------------------------------
# Ollama
# ---------------------------------------------------------------------------
DEFAULT_MODEL = "llama3.3:70b"
OLLAMA_URL = "http://localhost:11434"
LLM_TIMEOUT = 60

# ---------------------------------------------------------------------------
# Backend: Ollama (local) or OpenAI-compatible (GPT-4o / GPT-4o-mini)
# Set OPENAI_API_KEY env var and pass --model gpt-4o-mini to use OpenAI.
# ---------------------------------------------------------------------------

def discover_ollama() -> str:
    for path in glob.glob("/tmp/ollama-server-*.url"):
        try:
            url = open(path).read().strip()
            if not url:
                continue
            with urllib.request.urlopen(f"{url}/v1/models", timeout=3):
                return url
        except Exception:
            continue
    try:
        with urllib.request.urlopen(f"{OLLAMA_URL}/v1/models", timeout=3):
            return OLLAMA_URL
    except Exception:
        return ""


def _is_openai_model(model: str) -> bool:
    return model.startswith("gpt-") or model.startswith("o1") or model.startswith("o3")


def openai_generate(model: str, prompt: str, max_tokens: int = 64) -> str:
    api_key = os.environ.get("OPENAI_API_KEY", "")
    if not api_key:
        return ""
    body = json.dumps({
        "model": model,
        "messages": [{"role": "user", "content": prompt}],
        "max_tokens": max_tokens,
        "temperature": 0.15,
    }).encode()
    req = urllib.request.Request(
        "https://api.openai.com/v1/chat/completions",
        data=body,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=LLM_TIMEOUT) as r:
            resp = json.loads(r.read())
        return resp["choices"][0]["message"]["content"].strip()
    except Exception as e:
        sys.stderr.write(f"[openai] error: {e}\n")
        return ""
def llm_generate(endpoint: str, model: str, prompt: str, max_tokens: int = 64,
                 think_budget: int = 512, think: bool = True) -> str:
    """Route to OpenAI or Ollama depending on model name.

    think_budget: max tokens reserved for chain-of-thought when think=True.
    think=False: disables thinking for simple formatting tasks (stage 2).
    """
    if _is_openai_model(model):
        return openai_generate(model, prompt, max_tokens)
    options: dict = {"temperature": 0.15}
    if think:
        options["num_predict"] = think_budget + max_tokens
    else:
        options["num_predict"] = max_tokens
        options["think"] = False
    body = json.dumps({
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": options,
    }).encode()
    req = urllib.request.Request(
        f"{endpoint}/api/generate",
        data=body,
        headers={"Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=LLM_TIMEOUT) as r:
            raw = json.loads(r.read()).get("response", "").strip()
        sys.stderr.write(f"  [raw] {raw[:200]!r}\n")
        return re.sub(r"<think>.*?</think>", "", raw, flags=re.DOTALL).strip()
    except Exception as e:
        sys.stderr.write(f"  [ollama] error: {e}\n")
        return ""
# Keep backwards-compat alias
ollama_generate = llm_generate

# ---------------------------------------------------------------------------
# Template taxonomy
# ---------------------------------------------------------------------------

# Each template_group is (category, positive_templates, weight)
# Slots use {key} notation; filled from SLOT_VALUES[key]

SLOT_VALUES: dict[str, list[str]] = {
    "first_name":  ["Alex", "Maria", "James", "Priya", "Noah", "Sara", "Lena", "Kai",
                    "Sofia", "Omar", "Ingrid", "Yuki", "Chen", "Amara", "Felix"],
    "full_name":   ["Sarah Chen", "Alexander Novak", "Priya Sharma", "Lena Müller",
                    "Omar Hassan", "Sofia Rivera", "Kai Nakamura", "Amara Diallo",
                    "Felix Wagner", "Ingrid Larsen", "Yuki Tanaka", "Maria Santos"],
    "nickname":    ["Sara", "Alex", "Lex", "Kai", "Sof", "Len", "Mo", "Sam"],
    "pronoun_set": ["she/her", "he/him", "they/them", "she/they"],
    "degree":      ["Bachelor of Science", "Master's", "PhD", "MBA", "Bachelor of Arts",
                    "Master of Engineering", "Doctor of Philosophy"],
    "field":       ["computer science", "molecular biology", "data science", "economics",
                    "bioinformatics", "mechanical engineering", "linguistics", "psychology",
                    "mathematics", "architecture", "public health", "chemistry", "history",
                    "neuroscience", "environmental science"],
    "institution": ["DTU", "Copenhagen University", "MIT", "TU Berlin", "KU Leuven",
                    "Oxford", "Aarhus University", "NTNU", "Imperial College", "ETH Zurich"],
    "edu_status":  ["last spring", "last June", "in January", "two years ago", "last month"],
    "role":        ["software engineer", "data scientist", "nurse practitioner",
                    "postdoctoral researcher", "product manager", "UX designer",
                    "bioinformatician", "systems administrator", "consultant", "architect",
                    "teacher", "journalist", "sales engineer", "DevOps engineer",
                    "machine learning engineer", "clinical researcher", "statistician"],
    "company_type": ["biotech startup", "hospital", "university", "consulting firm",
                     "tech company", "NGO", "research institute", "government agency",
                     "financial firm", "media company", "pharmaceutical company"],
    "company":     ["Google", "Novo Nordisk", "Rigshospitalet", "McKinsey", "DTU",
                    "Roche", "CERN", "the BBC", "Maersk"],
    "sector":      ["biotech", "fintech", "healthcare", "academia", "the public sector",
                    "renewable energy", "e-commerce"],
    "city":        ["Copenhagen", "Berlin", "Aarhus", "Stockholm", "London", "Amsterdam",
                    "San Francisco", "Tokyo", "Nairobi", "Bangalore", "Lagos",
                    "Barcelona", "Vienna", "Seoul", "Toronto"],
    "neighborhood": ["near Nørreport station", "in Vesterbro", "in Frederiksberg",
                     "in Prenzlauer Berg", "near the university campus"],
    "country":     ["Denmark", "Germany", "Sweden", "Norway", "Japan", "Nigeria",
                    "India", "Brazil", "Portugal", "Canada", "the Netherlands"],
    "years":       ["one year", "two years", "three years", "five years", "six months",
                    "a year and a half", "four years"],
    "partner":     ["partner", "wife", "husband", "boyfriend", "girlfriend", "fiancée"],
    "n_kids":      ["one child", "two kids", "three children", "a daughter", "a son",
                    "a boy and a girl", "twins"],
    "kid_age":     ["4 and 7", "2 and 5", "8", "18 months", "6 and 10"],
    "diet":        ["vegetarian", "vegan", "pescatarian", "gluten-free", "plant-based",
                    "dairy-free"],
    "diet_years":  ["two years", "five years", "about ten years", "since college",
                    "since my diagnosis"],
    "food":        ["Thai food", "sushi", "Italian food", "Indian cuisine", "Korean BBQ",
                    "Ethiopian food", "tacos", "ramen"],
    "dish":        ["pad see ew", "tonkotsu ramen", "tikka masala", "pasta carbonara",
                    "bibimbap"],
    "allergy":     ["nuts", "shellfish", "gluten", "dairy", "eggs", "soy"],
    "drink":       ["coffee", "green tea", "matcha", "oat milk lattes", "herbal tea"],
    "cups":        ["two cups", "three cups", "four cups", "one cup"],
    "hobby":       ["birdwatching", "pottery", "watercolor painting", "sailing",
                    "woodworking", "photography", "brewing beer", "urban sketching",
                    "calligraphy", "beekeeping"],
    "sport":       ["marathon running", "crossfit", "cycling", "swimming", "rock climbing",
                    "tennis", "rowing", "triathlon", "yoga", "weightlifting"],
    "freq":        ["three times a week", "twice a week", "every morning", "on weekends"],
    "n_marathons": ["two", "three", "four", "five", "six"],
    "instrument":  ["guitar", "piano", "violin", "drums", "cello", "bass", "saxophone"],
    "genre":       ["jazz", "classical music", "folk", "electronic music", "hip-hop"],
    "artist":      ["Miles Davis", "Bach", "Radiohead", "Kendrick Lamar", "Björk"],
    "lang":        ["Danish", "French", "Mandarin", "Spanish", "Arabic", "German",
                    "Japanese", "Swahili", "Portuguese", "Korean"],
    "lang_level":  ["A2", "B1", "B2", "conversational", "near-fluent"],
    "device":      ["ThinkPad X1 Carbon", "MacBook Pro M3", "Framework laptop",
                    "Surface Pro", "Dell XPS 15", "MacBook Air M2"],
    "car":         ["2019 Volkswagen Golf", "a Tesla Model 3", "a secondhand Volvo",
                    "an electric bike — no car"],
    "home":        ["my apartment", "a flat with my partner", "a house outside the city",
                    "renting in the city center"],
    "cert":        ["PMP", "AWS Solutions Architect", "Certified Scrum Master",
                    "CFA", "CompTIA Security+", "Google Cloud Professional"],
    "health_cond": ["type 1 diabetes", "ADHD", "asthma", "celiac disease",
                    "hypothyroidism", "chronic migraine"],
    "age_diag":    ["12", "25", "35", "as a child", "last year", "in my twenties"],
    "n_countries": ["20", "27", "34", "40", "over 50"],
    "wfh_days":    ["Monday and Friday", "three days a week", "most days", "full-time"],
    "sober_years": ["one year", "three years", "five years", "six months"],
    "wake_time":   ["5:00", "5:30", "6:00", "6:30"],
    # For hard negatives
    "fn_name":     ["process_data", "compute_loss", "fetch_records", "parse_json",
                    "run_pipeline", "validate_input", "transform_batch"],
    "var_name":    ["result", "output", "data", "items", "records", "config", "model"],
    "lang_tech":   ["Python", "Rust", "TypeScript", "Go", "C++", "Kotlin", "Julia"],
    "error":       ["segfault", "null pointer exception", "stack overflow",
                    "out-of-memory error", "type error", "import error"],
    "tool":        ["Docker", "Kubernetes", "GitHub Actions", "Terraform", "Ansible",
                    "Prometheus", "Grafana"],
    "concept":     ["dependency injection", "monads", "backpropagation", "attention",
                    "gradient clipping", "tokenization", "embedding"],
    "colleague":   ["my colleague", "my manager", "my supervisor", "my co-author",
                    "my team lead", "my professor"],
    "colleague_role": ["a doctor", "a software engineer", "a data scientist",
                       "an ML researcher", "a senior developer", "a nurse"],
}

def fill(template: str) -> str:
    """Fill a template by randomly selecting from SLOT_VALUES for each {slot}."""
    def replace_slot(m):
        key = m.group(1)
        vals = SLOT_VALUES.get(key, [m.group(0)])
        return random.choice(vals)
    return re.sub(r'\{(\w+)\}', replace_slot, template)


# Positive templates: (category, template_string)
POSITIVE_TEMPLATES = [
    # --- Name ---
    ("name", "My name is {full_name}."),
    ("name", "I go by {first_name}. My full name is {full_name}."),
    ("name", "Call me {first_name}."),
    ("name", "My name is {full_name}, but everyone calls me {nickname}."),
    ("name", "People usually call me {nickname} — short for {full_name}."),
    ("name", "I prefer {first_name}, though officially it's {full_name}."),
    ("name", "My pronouns are {pronoun_set}."),
    ("name", "I use {pronoun_set} pronouns, by the way."),

    # --- Education ---
    ("education", "I graduated with a {degree} in {field} last spring."),
    ("education", "I just finished my {degree} in {field}."),
    ("education", "I'm currently doing my {degree} in {field} at {institution}."),
    ("education", "I have a {degree} in {field} from {institution}."),
    ("education", "I dropped out of my {degree} program to start a company."),
    ("education", "I got my {degree} in {field} {edu_status}."),
    ("education", "I'm a second-year {degree} student in {field}."),
    ("education", "I'm about halfway through my {degree} in {field}."),
    ("education", "I finished my undergraduate degree in {field} and went straight into industry."),
    ("education", "I'm a postdoc in {field} at {institution}."),
    ("education", "I'm in the middle of my PhD in {field} — third year now."),
    ("education", "I defended my {degree} thesis in {field} {edu_status}."),

    # --- Occupation ---
    ("occupation", "I work as a {role} at a {company_type}."),
    ("occupation", "I'm a {role} at {company}."),
    ("occupation", "I'm self-employed — I run a small {sector} consultancy."),
    ("occupation", "I used to be a teacher but I switched to {role} two years ago."),
    ("occupation", "I just got promoted to senior {role}."),
    ("occupation", "I'm currently between jobs, looking for {role} positions."),
    ("occupation", "I've been a freelance {role} for about {years}."),
    ("occupation", "I'm a {role} — been doing it for {years} now."),
    ("occupation", "I recently started as a {role} at a {company_type}."),
    ("occupation", "I transitioned from {sector} to {role} three years ago."),
    ("occupation", "I work in {sector} — specifically as a {role}."),
    ("occupation", "I run my own {company_type}, been at it for {years}."),
    ("occupation", "I just left my job as a {role} to do a PhD."),
    ("occupation", "I'm on sabbatical from my role as a {role}."),

    # --- Location current ---
    ("location", "I live in {city}, {neighborhood}."),
    ("location", "I moved to {city} from {country} {years} ago."),
    ("location", "I'm based in {city} for work."),
    ("location", "I'm originally from {city} but I've been in {country} for {years}."),
    ("location", "I grew up in a small town in {country}."),
    ("location", "My hometown is {city}, though I live in {city} now."),
    ("location", "I relocated to {city} last year for a new job."),
    ("location", "I've been living in {city} for {years}."),
    ("location", "I'm planning to move to {city} next year."),
    ("location", "I split my time between {city} and {city}."),
    ("location", "I'm temporarily in {city} for a six-month project."),

    # --- Family ---
    ("family", "I have {n_kids}, ages {kid_age}."),
    ("family", "I'm married and we just had our first child."),
    ("family", "I live with my {partner} of {years}."),
    ("family", "I'm single, no kids."),
    ("family", "My mom was a doctor and my dad was a teacher."),
    ("family", "I have a twin sister."),
    ("family", "I'm expecting my first child in a few months."),
    ("family", "I just got engaged."),
    ("family", "I'm divorced, co-parenting {n_kids}."),
    ("family", "I live alone since my {partner} moved abroad."),

    # --- Diet / food ---
    ("diet", "I'm {diet}, have been for {diet_years}."),
    ("diet", "I've been {diet} for {diet_years} now."),
    ("diet", "I love {food}, especially {dish}."),
    ("diet", "I'm allergic to {allergy}, so I have to be careful."),
    ("diet", "I don't drink {drink} — strictly a tea person."),
    ("diet", "I'm lactose intolerant, switched to plant-based milk."),
    ("diet", "I'm on a low-carb diet for health reasons."),
    ("diet", "I keep halal."),
    ("diet", "I keep kosher."),

    # --- Drink ---
    ("drink", "I drink about {cups} of {drink} a day."),
    ("drink", "I prefer {drink} over coffee."),
    ("drink", "I barely drink alcohol — maybe wine at dinner occasionally."),
    ("drink", "I stopped drinking {drink} and switched to {drink}."),

    # --- Hobbies ---
    ("hobby", "My hobby is {hobby}, I've been doing it for {years}."),
    ("hobby", "I started {hobby} classes {years} ago."),
    ("hobby", "I spend most of my free time doing {hobby}."),
    ("hobby", "I paint {genre} on weekends — just for fun."),
    ("hobby", "I've been learning {hobby} this summer."),
    ("hobby", "I play {instrument} in a band on weekends."),
    ("hobby", "I grew up listening to {genre} — {artist} especially."),
    ("hobby", "I mostly listen to podcasts, not much music."),

    # --- Sport ---
    ("sport", "I do {sport} {freq}."),
    ("sport", "I run marathons — I've done {n_marathons} so far."),
    ("sport", "I play chess competitively, rated around 1900."),
    ("sport", "I love hiking — I try to do a big trail every summer."),
    ("sport", "I've been getting into {sport} lately."),
    ("sport", "I used to do {sport} but switched to {sport} after my injury."),

    # --- Possessions ---
    ("possession", "I just bought a new laptop — a {device}."),
    ("possession", "I drive {car}."),
    ("possession", "I finally got a standing desk for my home office."),
    ("possession", "I own {home}."),
    ("possession", "I don't own a car — I cycle everywhere."),
    ("possession", "I've been renting for now while I figure out next steps."),

    # --- Achievements ---
    ("achievement", "I published my first paper in Nature last month."),
    ("achievement", "I got my {cert} certification in January."),
    ("achievement", "I'm a licensed architect."),
    ("achievement", "I just passed the bar exam."),
    ("achievement", "I won a regional cooking competition last year."),
    ("achievement", "I got promoted to principal engineer last quarter."),

    # --- Health ---
    ("health", "I have {health_cond}, diagnosed when I was {age_diag}."),
    ("health", "I'm recovering from knee surgery."),
    ("health", "I started therapy about six months ago and it's really helped."),
    ("health", "I was recently diagnosed with {health_cond}."),
    ("health", "I've been managing {health_cond} for {years} now."),

    # --- Travel ---
    ("travel", "I've been to {n_countries} countries so far."),
    ("travel", "I lived in {city} for {years} teaching English."),
    ("travel", "I just got back from a month in {country}."),

    # --- Language ---
    ("language", "I speak {lang}, {lang}, and some {lang}."),
    ("language", "I'm learning {lang}, about {lang_level} level."),
    ("language", "My native language is {lang} but I'm fluent in {lang}."),
    ("language", "I grew up bilingual — {lang} and {lang}."),

    # --- Habits / routine ---
    ("habit", "I have a morning routine — gym at 6, then work by 8."),
    ("habit", "I work from home {wfh_days} but go into the office the rest."),
    ("habit", "I've been sober for {sober_years}."),
    ("habit", "I meditate every morning, about 20 minutes."),
    ("habit", "My commute is brutal — 90 minutes each way."),
    ("habit", "I switched careers at 35 and I don't regret it."),
    ("habit", "I'm an early bird — I'm up by {wake_time} every day."),
    ("habit", "I'm a night owl, I do my best work after midnight."),
    ("habit", "I volunteer at a food bank on Saturdays."),
    ("habit", "I've never owned a car."),

    # --- Hard positives: personal fact in technical context ---
    ("hard_positive", "I'm running this on my {device} — might be an ARM issue."),
    ("hard_positive", "I'm writing this from {city}, so timezone is an issue for syncs."),
    ("hard_positive", "I work in {sector}, which is why I care about GDPR compliance here."),
    ("hard_positive", "As a {role}, I deal with this kind of issue daily."),
    ("hard_positive", "I spent {years} in {sector} before switching to {field}."),
    ("hard_positive", "I'm a {role} by training, not a pure developer, so bear with me."),
    ("hard_positive", "I use {device} as my main dev machine, in case that matters."),
    ("hard_positive", "I have {health_cond}, so I need to think about ergonomics carefully."),
    ("hard_positive", "I'm studying {field} at {institution}, this is for my thesis."),
    ("hard_positive", "I switched from {lang_tech} to {lang_tech} six months ago."),
]

# Negative templates: output is always `-`
NEGATIVE_TEMPLATES = [
    # Pure technical
    ("tech_question", "How do I implement {concept} in {lang_tech}?"),
    ("tech_question", "What's the time complexity of this algorithm?"),
    ("tech_question", "Can you help me debug this {error}?"),
    ("tech_question", "The build is still failing on CI."),
    ("tech_question", "This function seems to have a race condition."),
    ("tech_question", "How does {concept} work exactly?"),
    ("tech_question", "What's the difference between these two approaches?"),
    ("tech_question", "I can't reproduce the {error} on my machine."),
    ("tech_question", "Let me show you the stack trace."),
    ("tech_question", "Is there a way to make this more efficient?"),
    ("tech_question", "Which {tool} plugin would you recommend here?"),
    ("tech_question", "The tests pass locally but fail in CI."),

    # General knowledge
    ("general_knowledge", "What time is it in Tokyo right now?"),
    ("general_knowledge", "Who wrote War and Peace?"),
    ("general_knowledge", "What's the capital of {country}?"),
    ("general_knowledge", "Can you explain quantum entanglement?"),
    ("general_knowledge", "What's the best way to learn {lang_tech}?"),
    ("general_knowledge", "How does photosynthesis work?"),
    ("general_knowledge", "What's the population of {city}?"),

    # Statements about code/data — "my" refers to code artifact, not person
    ("my_code", "My {fn_name} function returns the wrong type."),
    ("my_code", "My {var_name} variable is undefined here."),
    ("my_code", "My model is overfitting on the validation set."),
    ("my_code", "My branch conflicts with main."),
    ("my_code", "My pipeline breaks when the input is empty."),
    ("my_code", "My test suite takes 20 minutes to run."),
    ("my_code", "My query is too slow — it times out after 30 seconds."),
    ("my_code", "My container won't start."),
    ("my_code", "My config file is being ignored."),
    ("my_code", "My API is returning 429s under load."),
    ("my_code", "My function is named {fn_name} — is that a bad name?"),
    ("my_code", "My loop iterates one extra time."),

    # Third-party statements — personal info about SOMEONE ELSE
    ("third_party", "{colleague} is a {colleague_role}."),
    ("third_party", "The user in the example is a software engineer."),
    ("third_party", "My friend moved to {city} recently."),
    ("third_party", "The character in the story lives in {city}."),
    ("third_party", "She told me she works in {sector}."),
    ("third_party", "Our professor has a PhD in {field}."),
    ("third_party", "{colleague} works from home on Fridays."),
    ("third_party", "The author of this paper is based in {city}."),

    # Hypotheticals / conditionals
    ("hypothetical", "If I were a {role}, how would I approach this?"),
    ("hypothetical", "Imagine I'm building a system for {sector}."),
    ("hypothetical", "What would you do if you had {health_cond}?"),
    ("hypothetical", "Suppose I lived in {city}."),
    ("hypothetical", "Let's say I'm a {role} who needs to..."),
    ("hypothetical", "In theory, if I studied {field}..."),

    # Greetings / meta / session management
    ("meta", "Let's continue from where we left off."),
    ("meta", "Can you summarize what we discussed?"),
    ("meta", "That's not quite what I meant."),
    ("meta", "Thanks, that helps."),
    ("meta", "Let me rephrase that."),
    ("meta", "Actually, forget what I said earlier."),
    ("meta", "I think we're going off track."),

    # Opinions / preferences about tools/topics (not personal identity facts)
    ("opinion", "I think {concept} is underrated."),
    ("opinion", "I prefer {lang_tech} over {lang_tech} for this kind of task."),
    ("opinion", "I don't like how {tool} handles configuration."),
    ("opinion", "I agree with your analysis."),
    ("opinion", "I'm not sure this approach scales."),
    ("opinion", "I wonder if that function should be memoized."),
    ("opinion", "I think the issue is in the parsing step."),

    # "I" statements that are not stable personal facts
    ("transient", "I just ran the tests and they fail."),
    ("transient", "I'm looking at the error message now."),
    ("transient", "I cloned the repo and built it."),
    ("transient", "I tried restarting the service."),
    ("transient", "I can see the issue in the logs."),
    ("transient", "I checked and the endpoint is responding."),
    ("transient", "I pushed the fix to the branch."),
    ("transient", "I need to step away for a bit."),
]

# ---------------------------------------------------------------------------
# Two-stage labeling
# ---------------------------------------------------------------------------

STAGE1_PROMPT = """You are a fact classifier. Given a user statement from a chat session, determine:
1. Does it contain a stable personal fact about the speaker (name, location, occupation, education, health, habits, relationships, possessions, skills)?
2. If yes, what category (name/location/occupation/education/health/diet/hobby/sport/family/language/possession/achievement/habit)?
3. What is the core fact in 5 words or less?

Output JSON only: {{"is_fact": true/false, "category": "...", "core": "..."}}
If no personal fact, output: {{"is_fact": false}}

Statement: "{text}"
JSON:"""

STAGE2_PROMPT = """Convert this personal fact to a 3rd-person retrieval hint (8-15 words, starting with "User", no first-person pronouns):
Category: {category}
Core fact: {core}
Original: "{text}"

Output only the hint sentence (8-15 words):"""

STAGE2_REPROMPT = """You must output a single sentence (8-15 words) starting with "User" that captures this personal fact.
Do not use I/my/me. Output ONLY the sentence.
Fact: {core}
Original statement: "{text}"
Hint:"""


def word_count(s: str) -> int:
    return len(s.split())

def is_valid_hint(h: str) -> bool:
    h = h.strip().strip('"').strip("'")
    if not h.startswith("User"):
        return False
    wc = word_count(h)
    if not (7 <= wc <= 18):  # slight tolerance
        return False
    if re.search(r'\b(I|my|me|I\'m|I\'ve|I\'d|myself)\b', h, re.IGNORECASE):
        return False
    return True
COMBINED_PROMPT = (
    'Given this user statement, determine if it contains a stable personal fact '
    '(identity, location, occupation, education, health, habits, relationships, '
    'possessions, skills). If yes, write a 3rd-person retrieval hint (8-15 words, '
    'starting with "User", no first-person pronouns like I/my/me).\n\n'
    'Output JSON only:\n'
    '- With fact: {{"is_fact": true, "hint": "User ..."}}\n'
    '- No fact:   {{"is_fact": false}}\n\n'
    'Statement: "{text}"\n'
    'JSON:'
)


def label_positive(endpoint: str, model: str, text: str) -> str | None:
    """Single-stage labeling with thinking: classify + render hint in one call."""
    raw = ollama_generate(endpoint, model,
                          COMBINED_PROMPT.format(text=text[:400]),
                          max_tokens=128, think_budget=512, think=True)
    try:
        m = re.search(r'\{.*?\}', raw, re.DOTALL)
        if not m:
            sys.stderr.write(f"  [label] no JSON in: {raw[:120]!r}\n")
            return None
        parsed = json.loads(m.group(0))
    except Exception as e:
        sys.stderr.write(f"  [label] parse error {e}: {raw[:120]!r}\n")
        return None

    if not parsed.get("is_fact", False):
        return None

    hint = parsed.get("hint", "").strip().strip('"').strip("'")
    sys.stderr.write(f"  [hint] {hint!r}\n")
    return hint if is_valid_hint(hint) else None
def confirm_negative(endpoint: str, model: str, text: str) -> bool:
    """Confirm a template-marked negative is genuinely non-personal."""
    prompt = (
        f'Does this statement contain a stable personal fact about the speaker '
        f'(identity, location, occupation, health, etc.)? Answer only "yes" or "no".\n'
        f'Statement: "{text[:300]}"\nAnswer:'
    )
    resp = ollama_generate(endpoint, model, prompt, max_tokens=16, think=False).lower().strip()
    return not resp.startswith("yes")
# ---------------------------------------------------------------------------
# Main generation loop
# ---------------------------------------------------------------------------

def generate_instances(templates: list[tuple[str, str]], n: int) -> list[str]:
    """Randomly sample n filled instances from templates."""
    results = []
    pool = list(templates)
    while len(results) < n:
        cat, tmpl = random.choice(pool)
        results.append((cat, fill(tmpl)))
    return results


def norm_key(text: str) -> str:
    return re.sub(r'\s+', ' ', text.lower().strip())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="/maps/projects/caeg/scratch/kbd606/tmp/hint_corpus_v2.jsonl")
    parser.add_argument("--model", default=DEFAULT_MODEL)
    parser.add_argument("--target", type=int, default=1500)
    parser.add_argument("--neg-ratio", type=float, default=0.35)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    random.seed(args.seed)

    endpoint = discover_ollama()
    if not endpoint:
        sys.exit("[gen] no Ollama endpoint found")
    sys.stderr.write(f"[gen] endpoint={endpoint} model={args.model} target={args.target}\n")

    n_neg = int(args.target * args.neg_ratio)
    n_pos = args.target - n_neg

    sys.stderr.write(f"[gen] target: {n_pos} positives + {n_neg} negatives\n")

    seen: set[str] = set()
    written = 0
    pos_written = neg_written = 0
    pos_skipped = neg_skipped = 0

    os.makedirs(os.path.dirname(args.out), exist_ok=True)

    # Resume: load existing entries so we don't overwrite or duplicate them.
    if os.path.exists(args.out):
        with open(args.out) as _rf:
            for _line in _rf:
                try:
                    _rec = json.loads(_line)
                    seen.add(norm_key(_rec.get("input", "")))
                    if _rec.get("output", "") == "-":
                        neg_written += 1
                    else:
                        pos_written += 1
                except Exception:
                    pass
        sys.stderr.write(f"[gen] resuming: {pos_written} pos + {neg_written} neg already written\n")

    with open(args.out, "a") as f:
        # --- Positives ---
        pos_instances = generate_instances(POSITIVE_TEMPLATES, n_pos * 3)  # oversample 3x
        for i, (cat, text) in enumerate(pos_instances):
            if pos_written >= n_pos:
                break
            key = norm_key(text)
            if key in seen:
                continue
            seen.add(key)

            sys.stderr.write(f"[pos {pos_written+1}/{n_pos}] {text[:60]!r}\n")
            if args.dry_run:
                hint = f"User [dry-run: {cat}]"
            else:
                hint = label_positive(endpoint, args.model, text)

            if not hint:
                pos_skipped += 1
                sys.stderr.write(f"  → skip (no valid hint)\n")
                continue

            rec = {"input": text, "output": hint}
            f.write(json.dumps(rec) + "\n")
            f.flush()
            pos_written += 1
            sys.stderr.write(f"  → {hint!r}\n")

        # --- Negatives ---
        neg_instances = generate_instances(NEGATIVE_TEMPLATES, n_neg * 3)
        for i, (cat, text) in enumerate(neg_instances):
            if neg_written >= n_neg:
                break
            key = norm_key(text)
            if key in seen:
                continue
            seen.add(key)

            sys.stderr.write(f"[neg {neg_written+1}/{n_neg}] {text[:60]!r}\n")
            if args.dry_run:
                ok = True
            else:
                ok = confirm_negative(endpoint, args.model, text)

            if not ok:
                neg_skipped += 1
                sys.stderr.write(f"  → skip (model says is_fact=true)\n")
                continue

            rec = {"input": text, "output": "-"}
            f.write(json.dumps(rec) + "\n")
            f.flush()
            neg_written += 1
            sys.stderr.write(f"  → -\n")

    written = pos_written + neg_written
    sys.stderr.write(
        f"\n[gen] done: {written} total "
        f"({pos_written} pos, {neg_written} neg) "
        f"| skipped {pos_skipped} pos, {neg_skipped} neg\n"
    )
    print(json.dumps({
        "written": written, "positives": pos_written, "negatives": neg_written,
        "pos_skipped": pos_skipped, "neg_skipped": neg_skipped, "out": args.out,
    }))


if __name__ == "__main__":
    main()
