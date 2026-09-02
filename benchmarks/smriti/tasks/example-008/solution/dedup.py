def dedup_preserve_order(items):
    """Remove duplicates from items, keeping first-occurrence order. Input
    can be large -- pick an approach that scales."""
    seen = set()
    out = []
    for item in items:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out
