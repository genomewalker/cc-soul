# =============================================================================
# Coherence (τₖ) - Integration Measurement
# =============================================================================

@mcp.tool()
def get_coherence() -> str:
    """Get current coherence (τₖ) - how integrated the soul is.

    τₖ emerges from three dimensions:
    - Instantaneous: Current state of each aspect
    - Developmental: Trajectory and stability over time
    - Meta: Self-awareness and integration depth
    """
    from .coherence import compute_coherence, format_coherence_display, record_coherence

    state = compute_coherence()
    record_coherence(state)  # Track history
    return format_coherence_display(state)


@mcp.tool()
def get_tau_k() -> str:
    """Get τₖ value - the coherence coefficient."""
    from .coherence import compute_coherence

    state = compute_coherence()
    return f"τₖ = {state.value:.2f} ({state.interpretation})"
