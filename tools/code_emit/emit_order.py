"""emit_order.py -- put the emit worklist in callee-before-caller order.

Address order is the obvious way to hand 514 functions out and the wrong one.
A function whose callees do not exist yet still has to link, so the build fills
them in with generated stubs (see gen_stubs.py); a test written against that
function then exercises the stub rather than the callee, and the only honest
thing such a test can assert is the part of the behaviour that does not depend
on the call.  Emitting callees first shrinks that hole to the cases where it is
unavoidable: recursion, mutual recursion, and calls that only exist through a
function-pointer table.

So the order here is a topological sort of the call graph restricted to the
functions routing.json actually lists, with the strongly connected components
collapsed first.  A cycle has no callee-first order by definition; its members
come out together, in address order, and whoever emits the first of them is
told that the rest of the cycle is still stubbed.

The graph is workspace/call_graph/graph.json, rebuilt from Ghidra by
tools/call_graph/BuildCallGraph.java.  It carries both the direct CALL edges
and the indirect ones recovered from dispatch tables; both are used, because a
table-dispatched callee is stubbed exactly like any other missing one.

Output: workspace/code_emit/emit_order.json -- the ordered address list plus
the cycles, so a run can say why two functions came out together.

Usage: python tools/code_emit/emit_order.py [--print]
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
GRAPH = ROOT / "workspace" / "call_graph" / "graph.json"
ROUTING = HERE / "data" / "routing.json"
OUT = ROOT / "workspace" / "code_emit" / "emit_order.json"


def load_graph():
    if not GRAPH.is_file():
        sys.exit("missing %s -- run tools/call_graph/BuildCallGraph.java first"
                 % GRAPH)
    return json.loads(GRAPH.read_text(encoding="utf-8"))


def load_routing():
    return json.loads(ROUTING.read_text(encoding="utf-8"))["functions"]


def edges_within(graph, roster):
    """caller -> set(callee), both ends in the roster, self-loops dropped.

    Self-recursion is a cycle of one and says nothing about ordering, so it is
    dropped here rather than turned into a one-member component.
    """
    out = {addr: set() for addr in roster}
    for row in graph.get("direct_edges", []):
        src, dst = row[0], row[1]
        if src in out and dst in roster and src != dst:
            out[src].add(dst)
    for row in graph.get("indirect_edges", []):
        src, dst = row[0], row[1]
        if src in out and dst in roster and src != dst:
            out[src].add(dst)
    return out


def sccs(nodes, succ):
    """Tarjan, iterative -- the recursion depth here would blow the stack."""
    index = {}
    low = {}
    on_stack = {}
    stack = []
    result = []
    counter = [0]

    for root in nodes:
        if root in index:
            continue
        work = [(root, iter(sorted(succ[root])))]
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stack.append(root)
        on_stack[root] = True
        while work:
            node, it = work[-1]
            advanced = False
            for nxt in it:
                if nxt not in index:
                    index[nxt] = low[nxt] = counter[0]
                    counter[0] += 1
                    stack.append(nxt)
                    on_stack[nxt] = True
                    work.append((nxt, iter(sorted(succ[nxt]))))
                    advanced = True
                    break
                if on_stack.get(nxt):
                    low[node] = min(low[node], index[nxt])
            if advanced:
                continue
            work.pop()
            if work:
                parent = work[-1][0]
                low[parent] = min(low[parent], low[node])
            if low[node] == index[node]:
                comp = []
                while True:
                    w = stack.pop()
                    on_stack[w] = False
                    comp.append(w)
                    if w == node:
                        break
                result.append(sorted(comp))
    return result


def order(roster, succ):
    """Callee-first: every component comes after the components it calls.

    Tarjan already emits components in reverse topological order of the
    condensation -- a component is closed only once everything reachable from
    it is closed -- which is callee-first as it stands.  It is re-derived here
    by an explicit Kahn pass anyway, because relying on that property silently
    would make a future change to the SCC routine reorder the whole worklist
    with nothing to notice it.
    """
    comps = sccs(sorted(roster), succ)
    comp_of = {}
    for i, comp in enumerate(comps):
        for addr in comp:
            comp_of[addr] = i

    # Condensation edges point caller-component -> callee-component; a
    # component is ready when every component it calls has been emitted.
    calls = [set() for _ in comps]
    callers = [set() for _ in comps]
    for addr in sorted(roster):
        for dst in sorted(succ[addr]):
            a, b = comp_of[addr], comp_of[dst]
            if a != b:
                calls[a].add(b)
                callers[b].add(a)

    pending = {i: len(calls[i]) for i in range(len(comps))}
    # Ties are broken by the lowest address in the component, so the order is
    # reproducible rather than dictionary-order.
    ready = sorted((i for i in pending if pending[i] == 0),
                   key=lambda i: comps[i][0])
    out = []
    while ready:
        i = ready.pop(0)
        out.append(i)
        freed = []
        for j in sorted(callers[i]):
            pending[j] -= 1
            if pending[j] == 0:
                freed.append(j)
        for j in sorted(freed, key=lambda k: comps[k][0]):
            ready.append(j)
        ready.sort(key=lambda k: comps[k][0])

    if len(out) != len(comps):
        sys.exit("condensation is not acyclic -- %d of %d components ordered"
                 % (len(out), len(comps)))

    addrs = []
    for i in out:
        addrs.extend(comps[i])
    return addrs, [comps[i] for i in out if len(comps[i]) > 1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--print", dest="show", action="store_true",
                    help="list the order on stdout instead of only writing it")
    args = ap.parse_args()

    graph = load_graph()
    routing = load_routing()
    roster = set(routing)
    succ = edges_within(graph, roster)
    addrs, cycles = order(roster, succ)

    if len(addrs) != len(roster):
        sys.exit("ordered %d of %d functions" % (len(addrs), len(roster)))

    # How many caller-callee pairs the order could not satisfy: exactly the
    # pairs inside a cycle, and the number worth watching when the graph is
    # rebuilt.
    rank = {a: i for i, a in enumerate(addrs)}
    late = [(a, b) for a in sorted(roster) for b in sorted(succ[a])
            if rank[b] > rank[a]]

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        "_doc": "Callee-before-caller emit order over routing.json's roster, "
                "from the call graph. Regenerate after rebuilding graph.json.",
        "order": addrs,
        "cycles": [[{"addr": a, "name": routing[a]["name"]} for a in comp]
                   for comp in cycles],
        "unsatisfied_pairs": [{"caller": a, "callee": b} for a, b in late],
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print("%d functions ordered, %d cycle(s), %d caller-before-callee pair(s) "
          "left" % (len(addrs), len(cycles), len(late)))
    for comp in cycles:
        print("  cycle: %s" % ", ".join("%s %s" % (a, routing[a]["name"])
                                        for a in comp))
    if args.show:
        for i, a in enumerate(addrs):
            print("%4d %s %-52s %s" % (i + 1, a, routing[a]["name"],
                                       routing[a]["target"]))
    print("written: %s" % OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
