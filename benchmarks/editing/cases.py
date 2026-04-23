"""Edit-pattern benchmark cases.

Taxonomy follows fastedit's P1-P22 pattern labels (parcadei/fastedit,
MIT license) so the results are comparable. Cases here are independently
authored.

Each case is (name, pattern, original, edit_args, expected) where
edit_args is a dict understood by the strategies module.

edit_args keys:
  old_str, new_str         file_patch primitive
  symbol, body             symbol_patch primitive
  snippet                  fastedit-style snippet with `# ... existing code ...`
"""

from __future__ import annotations

CASES: list[dict] = []

def _case(name, pattern, original, **edit_args):
    expected = edit_args.pop("expected")
    CASES.append({
        "name": name,
        "pattern": pattern,
        "original": original,
        "edit_args": edit_args,
        "expected": expected,
    })


# P1: add_guard — early return before existing body
_case("p1_empty_list", "add_guard",
    "def average(nums):\n    total = sum(nums)\n    return total / len(nums)\n",
    old_str="def average(nums):\n    total = sum(nums)",
    new_str="def average(nums):\n    if not nums:\n        return 0.0\n    total = sum(nums)",
    symbol="average",
    body="def average(nums):\n    if not nums:\n        return 0.0\n    total = sum(nums)\n    return total / len(nums)",
    snippet="def average(nums):\n    if not nums:\n        return 0.0\n    total = sum(nums)\n    # ... existing code ...",
    expected="def average(nums):\n    if not nums:\n        return 0.0\n    total = sum(nums)\n    return total / len(nums)\n",
)

_case("p1_none_guard", "add_guard",
    "def get_name(user):\n    return user.first_name + ' ' + user.last_name\n",
    old_str="def get_name(user):\n    return",
    new_str="def get_name(user):\n    if user is None:\n        return 'Anonymous'\n    return",
    symbol="get_name",
    body="def get_name(user):\n    if user is None:\n        return 'Anonymous'\n    return user.first_name + ' ' + user.last_name",
    snippet="def get_name(user):\n    if user is None:\n        return 'Anonymous'\n    # ... existing code ...",
    expected="def get_name(user):\n    if user is None:\n        return 'Anonymous'\n    return user.first_name + ' ' + user.last_name\n",
)

# P2: add_line — insert line between context anchors
_case("p2_add_log", "add_line",
    "def process(x):\n    y = x * 2\n    return y\n",
    old_str="    y = x * 2\n    return y",
    new_str="    y = x * 2\n    print(f'y={y}')\n    return y",
    symbol="process",
    body="def process(x):\n    y = x * 2\n    print(f'y={y}')\n    return y",
    snippet="def process(x):\n    y = x * 2\n    print(f'y={y}')\n    # ... existing code ...",
    expected="def process(x):\n    y = x * 2\n    print(f'y={y}')\n    return y\n",
)

# P3: change_line — replace one line
_case("p3_change_op", "change_line",
    "def compute(a, b):\n    result = a + b\n    return result\n",
    old_str="    result = a + b",
    new_str="    result = a * b",
    symbol="compute",
    body="def compute(a, b):\n    result = a * b\n    return result",
    snippet="def compute(a, b):\n    result = a * b\n    # ... existing code ...",
    expected="def compute(a, b):\n    result = a * b\n    return result\n",
)

# P4: wrap_block — try/except around existing
_case("p4_try_wrap", "wrap_block",
    "def parse(s):\n    n = int(s)\n    return n\n",
    old_str="def parse(s):\n    n = int(s)\n    return n",
    new_str="def parse(s):\n    try:\n        n = int(s)\n        return n\n    except ValueError:\n        return None",
    symbol="parse",
    body="def parse(s):\n    try:\n        n = int(s)\n        return n\n    except ValueError:\n        return None",
    snippet="def parse(s):\n    try:\n        # ... existing code ...\n    except ValueError:\n        return None",
    expected="def parse(s):\n    try:\n        n = int(s)\n        return n\n    except ValueError:\n        return None\n",
)

# P5: add_at_end — append after last line of function
_case("p5_append_log", "add_at_end",
    "def save(obj):\n    db.write(obj)\n",
    old_str="    db.write(obj)",
    new_str="    db.write(obj)\n    logger.info('saved')",
    symbol="save",
    body="def save(obj):\n    db.write(obj)\n    logger.info('saved')",
    snippet="def save(obj):\n    # ... existing code ...\n    logger.info('saved')",
    expected="def save(obj):\n    db.write(obj)\n    logger.info('saved')\n",
)

# P6: replace_block — drop lines, insert new
_case("p6_replace_loop", "replace_block",
    "def sum_pos(xs):\n    total = 0\n    for x in xs:\n        if x > 0:\n            total += x\n    return total\n",
    old_str="    total = 0\n    for x in xs:\n        if x > 0:\n            total += x",
    new_str="    total = sum(x for x in xs if x > 0)",
    symbol="sum_pos",
    body="def sum_pos(xs):\n    total = sum(x for x in xs if x > 0)\n    return total",
    snippet="def sum_pos(xs):\n    total = sum(x for x in xs if x > 0)\n    # ... existing code ...",
    expected="def sum_pos(xs):\n    total = sum(x for x in xs if x > 0)\n    return total\n",
)

# P7: change_signature — modify def line, keep body
_case("p7_add_type", "change_signature",
    "def greet(name):\n    return f'hi {name}'\n",
    old_str="def greet(name):",
    new_str="def greet(name: str) -> str:",
    symbol="greet",
    body="def greet(name: str) -> str:\n    return f'hi {name}'",
    snippet="def greet(name: str) -> str:\n    # ... existing code ...",
    expected="def greet(name: str) -> str:\n    return f'hi {name}'\n",
)

# P8: multi_insert — insert multiple lines
_case("p8_multi_assign", "multi_insert",
    "def pipeline(data):\n    return transform(data)\n",
    old_str="def pipeline(data):\n    return",
    new_str="def pipeline(data):\n    data = clean(data)\n    data = normalize(data)\n    return",
    symbol="pipeline",
    body="def pipeline(data):\n    data = clean(data)\n    data = normalize(data)\n    return transform(data)",
    snippet="def pipeline(data):\n    data = clean(data)\n    data = normalize(data)\n    # ... existing code ...",
    expected="def pipeline(data):\n    data = clean(data)\n    data = normalize(data)\n    return transform(data)\n",
)

# P9: class_method — modify one method in a class
_case("p9_method_only", "class_method",
    "class Cart:\n    def __init__(self):\n        self.items = []\n\n    def add(self, item):\n        self.items.append(item)\n\n    def total(self):\n        return sum(i.price for i in self.items)\n",
    old_str="    def add(self, item):\n        self.items.append(item)",
    new_str="    def add(self, item):\n        if item is None:\n            return\n        self.items.append(item)",
    symbol="Cart.add",
    body="    def add(self, item):\n        if item is None:\n            return\n        self.items.append(item)",
    snippet="class Cart:\n    # ... existing code ...\n    def add(self, item):\n        if item is None:\n            return\n        self.items.append(item)\n    # ... existing code ...",
    expected="class Cart:\n    def __init__(self):\n        self.items = []\n\n    def add(self, item):\n        if item is None:\n            return\n        self.items.append(item)\n\n    def total(self):\n        return sum(i.price for i in self.items)\n",
)

# P11: delete_lines — remove lines
_case("p11_drop_log", "delete_lines",
    "def handle(req):\n    print('start')\n    x = req.value\n    print('end')\n    return x\n",
    old_str="    print('start')\n    x = req.value\n    print('end')\n    return x",
    new_str="    x = req.value\n    return x",
    symbol="handle",
    body="def handle(req):\n    x = req.value\n    return x",
    snippet="def handle(req):\n    x = req.value\n    return x",
    expected="def handle(req):\n    x = req.value\n    return x\n",
)

# P12: full_replace — replace entire function body
_case("p12_rewrite", "full_replace",
    "def fib(n):\n    a, b = 0, 1\n    for _ in range(n):\n        a, b = b, a + b\n    return a\n",
    old_str="def fib(n):\n    a, b = 0, 1\n    for _ in range(n):\n        a, b = b, a + b\n    return a",
    new_str="def fib(n):\n    if n < 2:\n        return n\n    return fib(n-1) + fib(n-2)",
    symbol="fib",
    body="def fib(n):\n    if n < 2:\n        return n\n    return fib(n-1) + fib(n-2)",
    snippet="def fib(n):\n    if n < 2:\n        return n\n    return fib(n-1) + fib(n-2)",
    expected="def fib(n):\n    if n < 2:\n        return n\n    return fib(n-1) + fib(n-2)\n",
)

# P13: add_branch — add elif
_case("p13_add_elif", "add_branch",
    "def kind(x):\n    if x > 0:\n        return 'pos'\n    else:\n        return 'neg'\n",
    old_str="    if x > 0:\n        return 'pos'\n    else:\n        return 'neg'",
    new_str="    if x > 0:\n        return 'pos'\n    elif x == 0:\n        return 'zero'\n    else:\n        return 'neg'",
    symbol="kind",
    body="def kind(x):\n    if x > 0:\n        return 'pos'\n    elif x == 0:\n        return 'zero'\n    else:\n        return 'neg'",
    snippet="def kind(x):\n    if x > 0:\n        return 'pos'\n    elif x == 0:\n        return 'zero'\n    else:\n        return 'neg'",
    expected="def kind(x):\n    if x > 0:\n        return 'pos'\n    elif x == 0:\n        return 'zero'\n    else:\n        return 'neg'\n",
)

# P14: extend_literal — add items to list
_case("p14_add_item", "extend_literal",
    "ALLOWED = ['get', 'post']\n",
    old_str="ALLOWED = ['get', 'post']",
    new_str="ALLOWED = ['get', 'post', 'put', 'delete']",
    symbol="ALLOWED",
    body="ALLOWED = ['get', 'post', 'put', 'delete']",
    snippet="ALLOWED = ['get', 'post', 'put', 'delete']",
    expected="ALLOWED = ['get', 'post', 'put', 'delete']\n",
)

# P15: add_decorator
_case("p15_add_decorator", "add_decorator",
    "def memoized_view():\n    return compute()\n",
    old_str="def memoized_view():",
    new_str="@cache\ndef memoized_view():",
    symbol="memoized_view",
    body="@cache\ndef memoized_view():\n    return compute()",
    snippet="@cache\ndef memoized_view():\n    # ... existing code ...",
    expected="@cache\ndef memoized_view():\n    return compute()\n",
)

# P16: rename_variable
_case("p16_rename", "rename_variable",
    "def scale(xs):\n    total = 0\n    for x in xs:\n        total += x\n    return total\n",
    old_str="def scale(xs):\n    total = 0\n    for x in xs:\n        total += x\n    return total",
    new_str="def scale(xs):\n    acc = 0\n    for x in xs:\n        acc += x\n    return acc",
    symbol="scale",
    body="def scale(xs):\n    acc = 0\n    for x in xs:\n        acc += x\n    return acc",
    snippet="def scale(xs):\n    acc = 0\n    for x in xs:\n        acc += x\n    return acc",
    expected="def scale(xs):\n    acc = 0\n    for x in xs:\n        acc += x\n    return acc\n",
)

# P20: change_expression
_case("p20_change_arg", "change_expression",
    "def call():\n    return api.get(timeout=30)\n",
    old_str="    return api.get(timeout=30)",
    new_str="    return api.get(timeout=60)",
    symbol="call",
    body="def call():\n    return api.get(timeout=60)",
    snippet="def call():\n    return api.get(timeout=60)",
    expected="def call():\n    return api.get(timeout=60)\n",
)

# P21: add_parameter — update sig and body
_case("p21_add_param", "add_parameter",
    "def area(w, h):\n    return w * h\n",
    old_str="def area(w, h):\n    return w * h",
    new_str="def area(w, h, scale=1):\n    return w * h * scale",
    symbol="area",
    body="def area(w, h, scale=1):\n    return w * h * scale",
    snippet="def area(w, h, scale=1):\n    return w * h * scale",
    expected="def area(w, h, scale=1):\n    return w * h * scale\n",
)

# P22: remove_parameter
_case("p22_drop_param", "remove_parameter",
    "def send(msg, retries=3):\n    transport.send(msg)\n",
    old_str="def send(msg, retries=3):\n    transport.send(msg)",
    new_str="def send(msg):\n    transport.send(msg)",
    symbol="send",
    body="def send(msg):\n    transport.send(msg)",
    snippet="def send(msg):\n    transport.send(msg)",
    expected="def send(msg):\n    transport.send(msg)\n",
)

if __name__ == "__main__":
    print(f"{len(CASES)} cases across {len(set(c['pattern'] for c in CASES))} patterns")
