"""Randomized exploration beyond the exhaustive bounds (KRN-WAIT-022): seeded random walks
over generated scenarios, and a hypothesis stateful test when hypothesis is installed."""
import random
import unittest

from embmodel import (Cancel, Destroy, Give, Lock, Policy, Protocol, Resume, Scenario, SchedLock,
                      SchedUnlock, SetPrio, Sleep, Suspend, Take, Unlock, Yield, FOREVER, NO_WAIT,
                      random_walk)

OPS = ["take", "give", "sleep", "yield", "setprio", "suspend", "resume", "cancel", "destroy", "schedpair",
       "lockpair", "lockpair", "lock_nested"]


def gen_scenario(rng: random.Random, nthreads=4, nsems=2, nops=4, nmutexes=2) -> Scenario:
    tids = list(range(1, nthreads + 1))
    threads = {}
    for tid in tids:
        prog = []
        for _ in range(nops):
            kind = rng.choice(OPS)
            if kind == "take":
                prog.append(Take(rng.randrange(nsems), rng.choice([NO_WAIT, 1, 3, FOREVER])))
            elif kind == "give":
                prog.append(Give(rng.randrange(nsems)))
            elif kind == "sleep":
                prog.append(Sleep(rng.choice([0, 1, 2, FOREVER])))
            elif kind == "yield":
                prog.append(Yield())
            elif kind == "setprio":
                prog.append(SetPrio(rng.choice(tids), rng.randrange(1, 6)))
            elif kind == "suspend":
                prog.append(Suspend(rng.choice(tids)))
            elif kind == "resume":
                prog.append(Resume(rng.choice(tids)))
            elif kind == "cancel":
                prog.append(Cancel(rng.choice(tids)))
            elif kind == "destroy":
                prog.append(Destroy(rng.randrange(nsems)))
            elif kind == "lockpair":
                m = rng.randrange(nmutexes)
                prog.append(Lock(m, rng.choice([NO_WAIT, 1, 3, FOREVER])))
                prog.append(rng.choice([Yield(), Sleep(1), Give(rng.randrange(nsems))]))
                prog.append(Unlock(m))
            elif kind == "lock_nested":
                a, b = rng.randrange(nmutexes), rng.randrange(nmutexes)
                prog.append(Lock(a, rng.choice([2, FOREVER])))
                prog.append(Lock(b, rng.choice([2, FOREVER])))
                prog.append(Unlock(b))
                prog.append(Unlock(a))
            else:
                prog.append(SchedLock()); prog.append(Give(rng.randrange(nsems))); prog.append(SchedUnlock())
        threads[tid] = (rng.randrange(1, 6), tuple(prog))
    sems = {s: dict(count=rng.randrange(0, 2), policy=rng.choice([Policy.PRIORITY_FIFO, Policy.FIFO]))
            for s in range(nsems)}
    isrs = tuple((Give(rng.randrange(nsems)),) for _ in range(rng.randrange(0, 3)))
    mutexes = {}
    for m in range(nmutexes):
        proto = rng.choice([Protocol.INHERIT, Protocol.INHERIT, Protocol.CEILING, Protocol.NONE])
        mutexes[m] = dict(protocol=proto, ceiling=5 if proto == Protocol.CEILING else 0,
                          recursive=rng.random() < 0.3)
    return Scenario(name="random", threads=threads, sems=sems, mutexes=mutexes, isrs=isrs)


class RandomWalks(unittest.TestCase):
    def test_seeded_random_walks(self):
        for seed in range(300):
            rng = random.Random(seed)
            sc = gen_scenario(rng)
            with self.subTest(seed=seed):
                random_walk(sc, seed=seed, max_steps=300)


try:
    from hypothesis import given, settings, strategies as st
    HAVE_HYPOTHESIS = True
except ImportError:          # pragma: no cover
    HAVE_HYPOTHESIS = False


@unittest.skipUnless(HAVE_HYPOTHESIS, "hypothesis not installed")
class HypothesisWalks(unittest.TestCase):
    @settings(max_examples=200, deadline=None)
    @given(st.integers(min_value=0, max_value=2**31 - 1), st.integers(min_value=0, max_value=2**31 - 1))
    def test_random_scenarios(self, scenario_seed, walk_seed):
        sc = gen_scenario(random.Random(scenario_seed))
        random_walk(sc, seed=walk_seed, max_steps=300)


if __name__ == "__main__":
    unittest.main()
