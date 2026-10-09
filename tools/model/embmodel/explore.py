"""Exhaustive and randomized exploration of the reference model (KRN-WAIT-007, KRN-WAIT-022)."""
from __future__ import annotations

import random
from dataclasses import dataclass, field

from .kernel import Kernel, ModelError, Scenario, build


@dataclass
class Report:
    states: int = 0
    transitions: int = 0
    terminals: int = 0
    max_depth: int = 0
    outcomes: set = field(default_factory=set)
    stale_timeouts_max: int = 0

    def summary(self) -> str:
        return (f"states={self.states} transitions={self.transitions} terminals={self.terminals} "
                f"max_depth={self.max_depth} distinct_outcomes={len(self.outcomes)} "
                f"stale_timeouts_max={self.stale_timeouts_max}")


def explore(sc: Scenario, max_states: int = 500_000) -> Report:
    """Depth-first search over every interleaving at section granularity. Every reached
    state is checked against the invariants of SPEC-004 §5.4; every terminal state against
    the terminal properties. Raises ModelError on the first violation, with the path."""
    root = build(sc)
    root.check_invariants()
    rep = Report()
    visited = {root.key()}
    stack = [(root, 0, ())]
    while stack:
        k, depth, path = stack.pop()
        rep.states += 1
        rep.max_depth = max(rep.max_depth, depth)
        rep.stale_timeouts_max = max(rep.stale_timeouts_max, k.stale_timeouts)
        if rep.states > max_states:
            raise ModelError(f"state budget {max_states} exceeded for scenario {sc.name!r}")
        acts = k.enabled()
        if not acts:
            try:
                k.check_terminal()
            except ModelError as e:
                raise ModelError(f"{e}\n  scenario={sc.name!r}\n  path={path}") from None
            rep.terminals += 1
            rep.outcomes.add(k.outcome())
            continue
        for act in acts:
            n = k.clone()
            try:
                n.apply(act)
            except ModelError as e:
                raise ModelError(f"{e}\n  scenario={sc.name!r}\n  path={path + (act,)}") from None
            rep.transitions += 1
            key = n.key()
            if key not in visited:
                visited.add(key)
                stack.append((n, depth + 1, path + (act,)))
    return rep


def random_walk(sc: Scenario, seed: int, max_steps: int = 200) -> Kernel:
    """One random interleaving; invariants are checked at every step."""
    rng = random.Random(seed)
    k = build(sc)
    k.check_invariants()
    for _ in range(max_steps):
        acts = k.enabled()
        if not acts:
            k.check_terminal()
            break
        k.apply(rng.choice(acts))
    return k
