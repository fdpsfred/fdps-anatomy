"""Turn the raw call graph dump into the structural facts ticket 12 needs.

Reads ``workspace/call_graph/graph.json`` (produced by ``BuildCallGraph.java``)
and writes, into the same directory:

* ``report.md``           -- reachability, clusters, shared-helper ranking
* ``graph.dot``           -- Graphviz source for the whole graph
* ``backbone_queue.json`` -- BFS levels from the entry point, the worklist the
  backbone walk consumes
* ``islands.json``        -- every unreachable component in full, singletons
  included, so nothing the report table trims is lost

Nothing here judges an individual function; every number is a whole-binary
structural fact derived from the edges.
"""

import io
import json
import os
import sys
from collections import defaultdict, deque

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULT_DIR = os.path.join(REPO, "workspace", "call_graph")

# Functions with at least this many distinct callers are shared helpers: they
# have to travel as one unit when the binary is partitioned for parallel work.
SHARED_HELPER_MIN_CALLERS = 8


def load(path):
    with io.open(path, encoding="utf-8") as fh:
        return json.load(fh)


def build_adjacency(graph):
    """Merge direct and table-dispatched edges into one successor map."""
    succ = defaultdict(set)
    pred = defaultdict(set)
    kinds = defaultdict(set)
    for addr in (f["addr"] for f in graph["functions"]):
        succ[addr]
        pred[addr]
    for src, dst, kind, _site in graph["direct_edges"]:
        succ[src].add(dst)
        pred[dst].add(src)
        kinds[(src, dst)].add(kind)
    for src, dst, kind, view in graph["indirect_edges"]:
        succ[src].add(dst)
        pred[dst].add(src)
        kinds[(src, dst)].add("%s@%s" % (kind, view))
    return succ, pred, kinds


def bfs_levels(succ, start):
    """Depth of every function reachable from start, and the BFS order."""
    depth = {start: 0}
    order = [start]
    queue = deque([start])
    while queue:
        node = queue.popleft()
        for nxt in sorted(succ[node]):
            if nxt not in depth:
                depth[nxt] = depth[node] + 1
                order.append(nxt)
                queue.append(nxt)
    return depth, order


def weak_components(nodes, succ, pred):
    """Undirected connected components restricted to a node subset."""
    remaining = set(nodes)
    components = []
    while remaining:
        seed = min(remaining)
        component = set()
        queue = deque([seed])
        remaining.discard(seed)
        while queue:
            node = queue.popleft()
            component.add(node)
            for nxt in succ[node] | pred[node]:
                if nxt in remaining:
                    remaining.discard(nxt)
                    queue.append(nxt)
        components.append(sorted(component))
    components.sort(key=lambda c: (-len(c), c[0]))
    return components


def strong_components(nodes, succ):
    """Tarjan, iterative -- the graph is small but recursion depth is not free."""
    index = {}
    low = {}
    on_stack = set()
    stack = []
    result = []
    counter = [0]
    for root in sorted(nodes):
        if root in index:
            continue
        work = [(root, iter(sorted(succ[root])))]
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stack.append(root)
        on_stack.add(root)
        while work:
            node, children = work[-1]
            advanced = False
            for child in children:
                if child not in nodes:
                    continue
                if child not in index:
                    index[child] = low[child] = counter[0]
                    counter[0] += 1
                    stack.append(child)
                    on_stack.add(child)
                    work.append((child, iter(sorted(succ[child]))))
                    advanced = True
                    break
                if child in on_stack:
                    low[node] = min(low[node], index[child])
            if advanced:
                continue
            work.pop()
            if work:
                parent = work[-1][0]
                low[parent] = min(low[parent], low[node])
            if low[node] == index[node]:
                component = []
                while True:
                    popped = stack.pop()
                    on_stack.discard(popped)
                    component.append(popped)
                    if popped == node:
                        break
                if len(component) > 1:
                    result.append(sorted(component))
    result.sort(key=lambda c: (-len(c), c[0]))
    return result


def function_owner(funcs):
    """addr (hex string) -> the function whose body starts at or before it."""
    starts = sorted(int(a, 16) for a in funcs)
    import bisect

    def owner(addr):
        i = bisect.bisect_right(starts, int(addr, 16)) - 1
        return "%08x" % starts[i] if i >= 0 else None
    return owner


def address_taken_roots(graph):
    """Functions whose entry point is stored in a function-pointer table slot.

    These are the roots a static walk cannot reach through a CALL: the AIL
    driver tables with a run-time base, the 80x87 emulator's opcode tables,
    the chapter and menu dispatch tables whose dispatcher itself is only
    reached through a table.  A slot holding a mid-function address (a switch
    jump table) makes no root: it is a label, not an address-taken function."""
    roots = set()
    for run in graph["pointer_runs"]:
        for target, exact in zip(run["targets"], run["exact_entry"]):
            if exact:
                roots.add(target)
    return roots


def reach_from_roots(graph, funcs, roots):
    """Depth of every function reachable from roots, an edge into a function's
    body counting as reaching that function."""
    owner = function_owner(funcs)
    succ = defaultdict(set)
    for src, dst, _kind, _site in graph["direct_edges"] + graph["indirect_edges"]:
        target = dst if dst in funcs else owner(dst)
        if target is not None:
            succ[src].add(target)
    depth = {r: 0 for r in roots}
    queue = deque(sorted(roots))
    while queue:
        node = queue.popleft()
        for nxt in sorted(succ[node]):
            if nxt not in depth:
                depth[nxt] = depth[node] + 1
                queue.append(nxt)
    return depth, succ


# ghidra_snapshot/functions.txt: "address | body size | calling convention |
# signature source | stack purge | flags | tags | prototype".
SNAPSHOT_ADDRESS, SNAPSHOT_TAGS = 0, 6
POOL_TAG = "pool_"


def load_pools(path):
    """{addr: pool} from the pool_* tag of each function in the Ghidra snapshot."""
    pools = {}
    if not os.path.isfile(path):
        return pools
    with io.open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("#") or line.startswith(" ") or "|" not in line:
                continue
            cells = [c.strip() for c in line.split("|")]
            tags = cells[SNAPSHOT_TAGS].split(",")
            pools[cells[SNAPSHOT_ADDRESS].lower()] = next(
                (t[len(POOL_TAG):] for t in tags if t.startswith(POOL_TAG)), "?")
    return pools


def write_dot(path, graph, succ, depth):
    names = {f["addr"]: f["name"] for f in graph["functions"]}
    with io.open(path, "w", encoding="utf-8") as fh:
        fh.write("digraph fdps {\n")
        fh.write("  graph [rankdir=LR, ranksep=1.2];\n")
        fh.write("  node [shape=box, fontname=\"Consolas\", fontsize=9];\n")
        for addr in sorted(names):
            d = depth.get(addr)
            colour = "white" if d is None else "gray%d" % max(55, 95 - 3 * d)
            fh.write("  \"%s\" [label=\"%s\", style=filled, fillcolor=%s];\n"
                     % (addr, names[addr], colour))
        for src in sorted(succ):
            for dst in sorted(succ[src]):
                fh.write("  \"%s\" -> \"%s\";\n" % (src, dst))
        fh.write("}\n")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    graph = load(os.path.join(out_dir, "graph.json"))
    funcs = {f["addr"]: f for f in graph["functions"]}
    succ, pred, kinds = build_adjacency(graph)
    entry = graph["entry_point"]

    depth, order = bfs_levels(succ, entry)
    unreached = sorted(set(funcs) - set(depth))
    islands = weak_components(unreached, succ, pred)
    cycles = strong_components(set(funcs), succ)

    fan_in = sorted(funcs, key=lambda a: (-len(pred[a]), a))
    shared = [a for a in fan_in if len(pred[a]) >= SHARED_HELPER_MIN_CALLERS]

    by_level = defaultdict(list)
    for addr, d in depth.items():
        by_level[d].append(addr)
    for level in by_level.values():
        level.sort()

    write_dot(os.path.join(out_dir, "graph.dot"), graph, succ, depth)

    queue_path = os.path.join(out_dir, "backbone_queue.json")
    with io.open(queue_path, "w", encoding="utf-8") as fh:
        payload = {
            "entry_point": entry,
            "levels": [
                {
                    "depth": d,
                    "functions": [
                        {
                            "addr": a,
                            "name": funcs[a]["name"],
                            "size": funcs[a]["size"],
                            "callers": sorted(pred[a]),
                            "callees": sorted(succ[a]),
                        }
                        for a in by_level[d]
                    ],
                }
                for d in sorted(by_level)
            ],
        }
        fh.write(json.dumps(payload, indent=2, ensure_ascii=False))

    orphan_run_targets = {
        t for r in graph["pointer_runs"] if not r["views"] for t in r["targets"]
    }
    islands_path = os.path.join(out_dir, "islands.json")
    with io.open(islands_path, "w", encoding="utf-8") as fh:
        fh.write(json.dumps([
            {
                "size": len(island),
                "members": [
                    {
                        "addr": a,
                        "name": funcs[a]["name"],
                        "size": funcs[a]["size"],
                        "callers": sorted(pred[a]),
                        "callees": sorted(succ[a]),
                        "in_orphan_pointer_run": a in orphan_run_targets,
                    }
                    for a in island
                ],
            }
            for island in islands
        ], indent=2, ensure_ascii=False))

    lines = []
    add = lines.append
    add("# Call graph analysis -- FDPS.LE")
    add("")
    add("Generated by `tools/call_graph/analyze_graph.py`. Regenerate after any")
    add("Ghidra change that adds functions or resolves an indirect call.")
    add("")
    add("## Totals")
    add("")
    add("| metric | value |")
    add("| --- | --- |")
    add("| functions | %d |" % len(funcs))
    add("| direct call/jump edges | %d |" % len(graph["direct_edges"]))
    add("| table-dispatched edges | %d |" % len(graph["indirect_edges"]))
    add("| distinct edges | %d |" % sum(len(v) for v in succ.values()))
    add("| reachable from entry `%s` | %d |" % (entry, len(depth)))
    add("| unreachable | %d |" % len(unreached))
    add("| max depth | %d |" % max(depth.values()))
    add("")

    # Reachability with every address-taken function as an extra root: the
    # figure program_info/architecture.md quotes.
    roots = address_taken_roots(graph) | {entry}
    rdepth, rsucc = reach_from_roots(graph, funcs, roots)
    runreached = sorted(set(funcs) - set(rdepth))
    rpred = defaultdict(set)
    for src, dsts in rsucc.items():
        for dst in dsts:
            rpred[dst].add(src)
    rislands = weak_components(runreached, rsucc, rpred)
    pools = load_pools(os.path.join(REPO, "ghidra_snapshot", "functions.txt"))
    by_pool = defaultdict(int)
    for a in runreached:
        by_pool[pools.get(a, "?")] += 1
    isolated = sum(1 for c in rislands if len(c) == 1 and not rsucc[c[0]] and not rpred[c[0]])
    add("## Reachability from the entry point and every address-taken function")
    add("")
    add("Roots: the entry point plus the %d functions whose entry point is stored in"
        % (len(roots) - 1))
    add("a function-pointer table slot (`exact` above; a mid-function slot is a")
    add("switch label, not a root). An edge into a function's body reaches that")
    add("function.")
    add("")
    add("| metric | value |")
    add("| --- | --- |")
    add("| reachable | %d |" % sum(1 for a in rdepth if a in funcs))
    add("| unreachable | %d |" % len(runreached))
    add("| unreachable by pool | %s |" % ", ".join(
        "%s %d" % (p, n) for p, n in sorted(by_pool.items(), key=lambda kv: (-kv[1], kv[0]))))
    add("| max depth | %d |" % max(rdepth.values()))
    add("| weakly connected clusters among the unreachable | %d |" % len(rislands))
    add("| of which isolated (no edge either way) | %d |" % isolated)
    add("")
    add("| size | address range |")
    add("| --- | --- |")
    for island in rislands[:6]:
        add("| %d | `%s`-`%s` |" % (len(island), island[0], island[-1]))
    add("")
    add("Unreachable `fdps` functions: %s" % ", ".join(
        "`%s` %s" % (a, funcs[a]["name"]) for a in runreached if pools.get(a) == "fdps"))
    add("")

    add("## Depth from the entry point")
    add("")
    add("| depth | functions |")
    add("| --- | --- |")
    for d in sorted(by_level):
        add("| %d | %d |" % (d, len(by_level[d])))
    add("")

    add("## Function-pointer tables")
    add("")
    add("A run is a maximal sequence of dword slots pointing into code. A view is")
    add("a slot that a `CALL`/`JMP` names as its dispatch base; the views of a run")
    add("tile it, so every slot is attributed to exactly one dispatcher.")
    add("")
    add("`dispatch` is the instruction that reads the view: a `JMP` view is a `switch`")
    add("jump table, a `CALL` view is a function-pointer table. `exact` counts the")
    add("slots holding a function entry point rather than an address in mid-function;")
    add("a jump table scores zero there unless Ghidra has carved one of its labels")
    add("out as a function of its own.")
    add("")
    add("| run | slots | view | entries | dispatch | exact | dispatchers |")
    add("| --- | --- | --- | --- | --- | --- | --- |")
    dispatched = [r for r in graph["pointer_runs"] if r["views"]]
    for run in dispatched:
        for i, view in enumerate(run["views"]):
            first = view["first_slot"]
            exact = run["exact_entry"][first:first + view["entries"]]
            add("| %s | %s | `%s` | %d | %s | %d | %s |" % (
                "`%s`" % run["base"] if i == 0 else "",
                run["slots"] if i == 0 else "",
                view["base"], view["entries"],
                "/".join(view["dispatch"]), sum(1 for e in exact if e),
                ", ".join("`%s`" % c for c in view["callers"])))
    add("")

    orphan_runs = [r for r in graph["pointer_runs"] if not r["views"]]
    orphan_targets = sorted({t for r in orphan_runs for t in r["targets"]})
    add("### Runs with no dispatcher")
    add("")
    add("%d runs covering %d slots have no cross-reference at all: their base is"
        % (len(orphan_runs), sum(r["slots"] for r in orphan_runs)))
    add("computed at run time, so no caller can be attributed and no edge is emitted.")
    add("They resolve to %d distinct functions, of which %d are otherwise unreachable."
        % (len(orphan_targets), len([t for t in orphan_targets if t not in depth])))
    add("")
    add("| run | slots | first target | last target |")
    add("| --- | --- | --- | --- |")
    for run in orphan_runs:
        add("| `%s` | %d | `%s` | `%s` |"
            % (run["base"], run["slots"], run["targets"][0], run["targets"][-1]))
    add("")

    add("## Unreachable clusters")
    add("")
    add("Weakly connected components among the %d functions the entry point cannot"
        % len(unreached))
    add("reach. Each is either dead code the linker pulled in or a cluster whose")
    add("only inbound edge is an unresolved indirect call.")
    add("")
    add("| size | address range | roots |")
    add("| --- | --- | --- |")
    multi = [c for c in islands if len(c) > 1]
    singles = [c for c in islands if len(c) == 1]
    for island in multi:
        roots = [a for a in island if not pred[a]]
        add("| %d | `%s`-`%s` | %s |"
            % (len(island), island[0], island[-1],
               ", ".join("`%s`" % r for r in roots[:4]) + (" ..." if len(roots) > 4 else "")))
    add("")
    add("A further %d components are a single function with no edge in either"
        % len(singles))
    add("direction. Every component, singletons included, is listed in `islands.json`.")
    add("")

    add("## Shared helpers")
    add("")
    add("Functions with %d or more distinct callers. These are the clusters that"
        % SHARED_HELPER_MIN_CALLERS)
    add("must stay in one partition when work is split up.")
    add("")
    add("| function | callers | callees | size | depth |")
    add("| --- | --- | --- | --- | --- |")
    for addr in shared:
        add("| `%s` %s | %d | %d | %d | %s |"
            % (addr, funcs[addr]["name"], len(pred[addr]), len(succ[addr]),
               funcs[addr]["size"],
               depth[addr] if addr in depth else "-"))
    add("")

    add("## Recursion")
    add("")
    if not cycles:
        add("No strongly connected component larger than one function: the call")
        add("graph is acyclic apart from direct self-recursion.")
    else:
        add("| size | members |")
        add("| --- | --- |")
        for cycle in cycles:
            add("| %d | %s |" % (len(cycle), ", ".join("`%s`" % m for m in cycle)))
    add("")

    report = os.path.join(out_dir, "report.md")
    with io.open(report, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))

    print("reachable=%d unreached=%d islands=%d shared_helpers=%d cycles=%d"
          % (len(depth), len(unreached), len(islands), len(shared), len(cycles)))
    print("written: %s, graph.dot, backbone_queue.json, islands.json" % report)


if __name__ == "__main__":
    main()
